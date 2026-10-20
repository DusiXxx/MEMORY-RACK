#include "PhotoWorker.h"
#include "Recognizer.h"

namespace rm
{
std::atomic<int> PhotoWorker::liveThreads { 0 };

PhotoWorker::PhotoWorker() : juce::Thread ("Rack Memory photo") {}
PhotoWorker::~PhotoWorker()   { shutdown(); }

void PhotoWorker::submit (const juce::String& key, Task task)
{
    {
        const juce::ScopedLock sl (lock);
        if (stopping) return;
        auto obsolete = [&key] (const Job& j) { return key == "*" || j.key == key || j.key == "*"; };
        for (auto& j : queue) if (obsolete (j)) j.flag->store (true);
        if (running && current.flag != nullptr && obsolete (current)) current.flag->store (true);
        queue.push_back ({ key, std::move (task), std::make_shared<std::atomic<bool>> (false) });
    }
    if (! isThreadRunning())
    {
        ++liveThreads;
        if (! startThread (juce::Thread::Priority::low)) --liveThreads;
    }
    wake.signal();
}

void PhotoWorker::cancelAll()
{
    const juce::ScopedLock sl (lock);
    for (auto& j : queue) j.flag->store (true);
    if (running && current.flag != nullptr) current.flag->store (true);
}

void PhotoWorker::shutdown()
{
    {
        const juce::ScopedLock sl (lock);
        stopping = true;
    }
    cancelAll();
    signalThreadShouldExit();
    wake.signal();
    if (isThreadRunning())
    {
        stopThread (20000);           // rozbehla analyza skonci u nejblizsi kontroly (zlomky sekundy)
        --liveThreads;
    }
}

bool PhotoWorker::isBusy() const
{
    const juce::ScopedLock sl (lock);
    return running || ! queue.empty();
}

int PhotoWorker::queued() const
{
    const juce::ScopedLock sl (lock);
    return (int) queue.size();
}

void PhotoWorker::run()
{
    while (! threadShouldExit())
    {
        Job job;
        {
            const juce::ScopedLock sl (lock);
            if (! queue.empty())
            {
                job = std::move (queue.front());
                queue.pop_front();
                current = job;
                running = true;
            }
        }
        if (job.task == nullptr)
        {
            wake.wait (-1);           // spi, dokud neprijde dalsi fotka (zadne dotazovani)
            continue;
        }
        if (threadShouldExit()) job.flag->store (true);
        ++started;
        {
            const rmcv::CancelScope scope (job.flag.get());
            job.task (*job.flag);
        }
        rmcv::releaseWorkingMemory();          // pracovni kopie fotky a body se mezi ulohami nedrzi v pameti
        if (job.flag->load()) ++cancelledJobs;
        ++finished;
        const juce::ScopedLock sl (lock);
        running = false;
        current = {};
    }
    // co zbylo ve fronte: jen uklid (priznak zruseno)
    std::deque<Job> rest;
    {
        const juce::ScopedLock sl (lock);
        rest.swap (queue);
    }
    for (auto& j : rest)
    {
        j.flag->store (true);
        const rmcv::CancelScope scope (j.flag.get());
        j.task (*j.flag);
        ++cancelledJobs;
    }
}

void logPhotoJob (const Library& lib, const juce::File& source, const juce::Image& photo,
                  const std::vector<std::pair<juce::String, const rmcv::Result*>>& results, double loadMs, double analyzeMs)
{
    juce::String line;
    line << juce::Time::getCurrentTime().toISO8601 (true) << "  " << rmcv::kAlgorithmVersion
         << "  " << source.getFileName() << " " << photo.getWidth() << "x" << photo.getHeight()
         << "  nacteni " << juce::roundToInt (loadMs) << " ms, analyza " << juce::roundToInt (analyzeMs) << " ms";
    for (auto& r : results)
    {
        if (r.second == nullptr) continue;
        const auto& x = *r.second;
        line << "\n    " << r.first << ": " << (x.cancelled ? "zruseno" : x.aligned ? "nalezen" : "nenalezen")
             << ", body " << x.keypoints << ", precteno " << x.read << "/" << x.readable
             << (x.fromCache ? ", z pameti" : "") << (x.message.isNotEmpty() ? ", " + x.message : juce::String());
    }
    auto f = lib.reportsDir().getChildFile ("photo-analysis.log");
    f.getParentDirectory().createDirectory();
    if (f.getSize() > 512 * 1024)
    {
        auto text = f.loadFileAsString();
        f.replaceWithText (text.substring (text.length() / 2));
    }
    f.appendText (line + "\n", false, false, "\n");
}
}

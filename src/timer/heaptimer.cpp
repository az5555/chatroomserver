// SPDX-License-Identifier: Apache-2.0
/*
 * Original: TinyWebServer-master (author: mark)
 * Modifications: YourName (2026-04-04) - brief note
 */

#include "heaptimer.h"

void HeapTimer::adjust(int id, int timeout)
{
    assert(!heap_.empty() && ref_.count(id) != 0);
    heap_[ref_[id]].expires = Clock::now() + MS(timeout);
    siftdown_(ref_[id], heap_.size());
}

void HeapTimer::add(int id, int timeout, TimeoutCallBack cb)
{
    assert(id >= 0);
    size_t i;
    if (ref_.count(id))
    {
        i = ref_[id];
        heap_[i].expires = Clock::now() + MS(timeout);
        heap_[i].cb = cb;
        if (!siftdown_(i, heap_.size()))
        {
            siftup_(i);
        }
    }
    else
    {
        i = heap_.size();
        ref_[id] = i;
        heap_.push_back({id, Clock::now() + MS(timeout), cb});
        siftup_(i);
    }
}

void HeapTimer::doWork(int id)
{
    if (heap_.empty() || ref_.count(id) == 0)
    {
        return;
    }
    size_t i = ref_[id];
    heap_[i].cb();
    del_(i);
}

void HeapTimer::tick()
{
    while (!heap_.empty())
    {
        TimerNode node = heap_.front();
        if (std::chrono::duration_cast<MS>(node.expires - Clock::now()).count() > 0)
        {
            break;
        }
        node.cb();
        pop();
    }
}

int HeapTimer::GetNextTick()
{
    tick();
    size_t res = 0;
    if (!heap_.empty())
    {
        res = std::chrono::duration_cast<MS>(heap_.front().expires - Clock::now()).count();
        if (res < 0)
        {
            res = 0;
        }
    }
    return res;
}

void HeapTimer::pop()
{
    if (heap_.empty())
    {
        return;
    }
    del_(0);
}

void HeapTimer::clear()
{
    ref_.clear();
    heap_.clear();
}

void HeapTimer::del_(size_t index)
{
    assert(!heap_.empty() && index >= 0 && index < heap_.size());
    size_t i = index;
    size_t n = heap_.size() - 1;
    assert(i <= n);
    if (i < n)
    {
        SwapNode_(i, n);
        if (!siftdown_(i, n))
        {
            siftup_(i);
        }
    }
    ref_.erase(heap_.back().id);
    heap_.pop_back();
}

void HeapTimer::siftup_(size_t index)
{
    assert(index >= 0 && index < heap_.size());
    while (index > 0)
    {
        size_t parent = (index - 1) / 2;
        if (heap_[parent] < heap_[index])
        {
            break;
        }
        SwapNode_(parent, index);
        index = parent;
    }
}

bool HeapTimer::siftdown_(size_t index, size_t n)
{
    assert(index >= 0 && index < heap_.size());
    assert(n >= 0 && n <= heap_.size());
    size_t i = index;
    size_t j = 2 * i + 1;
    while (j < n)
    {
        if (j + 1 < n && heap_[j + 1] < heap_[j])
        {
            j++;
        }
        if (heap_[i] < heap_[j])
        {
            break;
        }
        SwapNode_(i, j);
        i = j;
        j = 2 * i + 1;
    }
    return i > index;
}

void HeapTimer::SwapNode_(size_t i, size_t j)
{
    assert(i >= 0 && i < heap_.size());
    assert(j >= 0 && j < heap_.size());
    std::swap(heap_[i], heap_[j]);
    ref_[heap_[i].id] = i;
    ref_[heap_[j].id] = j;
}
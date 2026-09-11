#include "virtqueue_ops.hpp"
#include <string.h>
#include <stdint.h>
#include <stddef.h>

namespace {

static inline size_t align_up(size_t value, size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

}

VirtQueueView virtqueue_ops::view_from_mem(
    void* mem,
    uint32_t qsize
)
{
    VirtQueueView v{};

    if (mem == nullptr || qsize == 0)
        return v;

    uint8_t* p =
        static_cast<uint8_t*>(mem);

    // Descriptor table.
    const size_t desc_size =
        sizeof(VirtqDesc) * qsize;

    v.desc =
        reinterpret_cast<VirtqDesc*>(p);

    p += desc_size;

    // Available ring.
    //
    // struct virtq_avail:
    //   flags
    //   idx
    //   ring[qsize]
    //
    const size_t avail_size =
        sizeof(uint16_t) * 2 +
        sizeof(uint16_t) * qsize;

    v.avail =
        reinterpret_cast<VirtqAvail*>(p);

    p += avail_size;

    // Legacy VirtIO requires the used ring to start
    // at the next 4096-byte boundary.
    uintptr_t current =
        reinterpret_cast<uintptr_t>(p);

    current =
        (current + 4095u) &
        ~static_cast<uintptr_t>(4095u);

    p =
        reinterpret_cast<uint8_t*>(current);

    v.used =
        reinterpret_cast<VirtqUsed*>(p);

    v.size = qsize;
    v.last_used_idx = 0;

    return v;
}


void virtqueue_ops::init_rings(
    VirtQueueView* v
)
{
    if (v == nullptr)
        return;

    if (v->desc == nullptr ||
        v->avail == nullptr ||
        v->used == nullptr ||
        v->size == 0)
        return;

    memset(
        v->desc,
        0,
        sizeof(VirtqDesc) * v->size
    );

    v->avail->flags = 0;
    v->avail->idx = 0;

    for (uint32_t i = 0;
         i < v->size;
         ++i)
    {
        v->avail->ring[i] = 0;
    }

    v->used->flags = 0;
    v->used->idx = 0;

    for (uint32_t i = 0;
         i < v->size;
         ++i)
    {
        v->used->ring[i].id = 0;
        v->used->ring[i].len = 0;
    }

    v->last_used_idx = 0;
}


void virtqueue_ops::set_descriptor(
    VirtQueueView* v,
    uint32_t idx,
    uint64_t addr,
    uint32_t len,
    uint16_t flags,
    uint16_t next
)
{
    if (v == nullptr)
        return;

    if (v->desc == nullptr)
        return;

    if (idx >= v->size)
        return;

    v->desc[idx].addr = addr;
    v->desc[idx].len = len;
    v->desc[idx].flags = flags;
    v->desc[idx].next = next;
}


void virtqueue_ops::submit_descriptor(
    VirtQueueView* v,
    uint32_t idx
)
{
    if (v == nullptr)
        return;

    if (v->avail == nullptr)
        return;

    if (v->size == 0)
        return;

    if (idx >= v->size)
        return;

    const uint16_t slot =
        static_cast<uint16_t>(
            v->avail->idx % v->size
        );

    v->avail->ring[slot] =
        static_cast<uint16_t>(idx);

    // Compiler + CPU ordering barrier.
    __asm__ volatile("" ::: "memory");

    ++v->avail->idx;

    __asm__ volatile("" ::: "memory");
}


bool virtqueue_ops::try_dequeue_used(
    VirtQueueView* v,
    uint32_t* id_out,
    uint32_t* len_out
)
{
    if (v == nullptr)
        return false;

    if (v->used == nullptr)
        return false;

    if (v->size == 0)
        return false;

    __asm__ volatile("" ::: "memory");

    const uint16_t current =
        v->used->idx;

    if (v->last_used_idx == current)
        return false;

    const uint16_t slot =
        static_cast<uint16_t>(
            v->last_used_idx % v->size
        );

    const uint32_t id =
        v->used->ring[slot].id;

    const uint32_t len =
        v->used->ring[slot].len;

    if (id_out != nullptr)
        *id_out = id;

    if (len_out != nullptr)
        *len_out = len;

    ++v->last_used_idx;

    __asm__ volatile("" ::: "memory");

    return true;
}

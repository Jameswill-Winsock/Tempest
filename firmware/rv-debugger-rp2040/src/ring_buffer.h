/**
 * @file ring_buffer.h
 * @brief Minimal lock-free (single producer / single consumer) ring buffer.
 *
 * Replaces the bl_mcu_sdk Ring_Buffer_Type used by the BL702 firmware.
 * Sizes must be a power of two.
 */
#ifndef _RING_BUFFER_H
#define _RING_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint8_t *buffer;
    uint32_t size; /* power of two */
    uint32_t mask;
    volatile uint32_t head; /* write index */
    volatile uint32_t tail; /* read index  */
} ring_buffer_t;

static inline void rb_init(ring_buffer_t *rb, uint8_t *mem, uint32_t size)
{
    rb->buffer = mem;
    rb->size = size;
    rb->mask = size - 1u;
    rb->head = 0;
    rb->tail = 0;
}

static inline uint32_t rb_length(const ring_buffer_t *rb)
{
    return (uint32_t)(rb->head - rb->tail);
}

static inline uint32_t rb_free(const ring_buffer_t *rb)
{
    return rb->size - rb_length(rb);
}

static inline bool rb_empty(const ring_buffer_t *rb)
{
    return rb->head == rb->tail;
}

static inline void rb_write_byte(ring_buffer_t *rb, uint8_t data)
{
    if (rb_free(rb) == 0) {
        return; /* drop, as upstream does when the ringbuffer is full */
    }
    rb->buffer[rb->head & rb->mask] = data;
    rb->head++;
}

static inline uint32_t rb_write(ring_buffer_t *rb, const uint8_t *data, uint32_t len)
{
    uint32_t space = rb_free(rb);
    if (len > space) {
        len = space;
    }
    for (uint32_t i = 0; i < len; i++) {
        rb->buffer[(rb->head + i) & rb->mask] = data[i];
    }
    rb->head += len;
    return len;
}

static inline uint32_t rb_read(ring_buffer_t *rb, uint8_t *data, uint32_t len)
{
    uint32_t avail = rb_length(rb);
    if (len > avail) {
        len = avail;
    }
    for (uint32_t i = 0; i < len; i++) {
        data[i] = rb->buffer[(rb->tail + i) & rb->mask];
    }
    rb->tail += len;
    return len;
}

#endif /* _RING_BUFFER_H */

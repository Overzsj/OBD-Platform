#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "tft_ili9488.h"

namespace obd {
// Two producer-owned strip buffers. A queued fence returns ownership only
// after the SPI worker has consumed every rectangle referencing that buffer.
class TftTransferQueue {
public:
    void begin(Ili9488* display) {
        if (enabled_) return;
        display_=display;
        queue_=xQueueCreate(8,sizeof(Job));
        frameDone_=xSemaphoreCreateBinary();
        for (auto& slot:slots_)slot=xSemaphoreCreateBinary();
        if (!queue_ || !frameDone_ || !slots_[0] || !slots_[1]) { cleanup();return; }
        for (auto slot:slots_)xSemaphoreGive(slot);
        if (xTaskCreatePinnedToCore(worker,"tft_spi",4096,this,1,&task_,0)!=pdPASS) {
            cleanup();return;
        }
        enabled_=true;
    }
    void acquire(uint8_t slot) { if (enabled_)xSemaphoreTake(slots_[slot],portMAX_DELAY); }
    void release(uint8_t slot) { if (enabled_)fence(slots_[slot]); }
    void finish() {
        if (!enabled_)return;
        fence(frameDone_);xSemaphoreTake(frameDone_,portMAX_DELAY);
    }
    bool enabled() const { return enabled_; }
    void writeRect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint16_t* pixels,uint16_t stride) {
        if (!enabled_) { display_->writeRect(x,y,w,h,pixels,stride);return; }
        const Job job{x,y,w,h,stride,pixels,nullptr};
        xQueueSend(queue_,&job,portMAX_DELAY);
    }
private:
    struct Job {
        uint16_t x,y,w,h,stride;
        const uint16_t* pixels;
        SemaphoreHandle_t done;
    };
    void fence(SemaphoreHandle_t done) {
        const Job job{0,0,0,0,0,nullptr,done};xQueueSend(queue_,&job,portMAX_DELAY);
    }
    static void worker(void* arg) {
        auto& self=*static_cast<TftTransferQueue*>(arg);Job job;
        for (;;) if (xQueueReceive(self.queue_,&job,portMAX_DELAY)==pdTRUE) {
            if (job.done)xSemaphoreGive(job.done);
            else self.display_->writeRect(job.x,job.y,job.w,job.h,job.pixels,job.stride);
        }
    }
    void cleanup() {
        if (queue_)vQueueDelete(queue_);queue_=nullptr;
        if (frameDone_)vSemaphoreDelete(frameDone_);frameDone_=nullptr;
        for (auto& slot:slots_) { if (slot)vSemaphoreDelete(slot);slot=nullptr; }
    }
    Ili9488* display_=nullptr;
    QueueHandle_t queue_=nullptr;
    SemaphoreHandle_t slots_[2]{},frameDone_=nullptr;
    TaskHandle_t task_=nullptr;
    bool enabled_=false;
};
} // namespace obd

#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstddef>

namespace llcv::recording {
// Only recorder-owned 16-bit PCM is modified; sample count and timing stay intact.
inline void ApplyRecordingAudioGain(void* data, size_t bytes, int percent) noexcept {
    percent=std::clamp(percent,0,100);
    if(percent==100)return;
    if(percent==0){std::memset(data,0,bytes);return;}
    auto* pcm=static_cast<unsigned char*>(data);
    for(size_t i=0;i+sizeof(int16_t)<=bytes;i+=sizeof(int16_t)){
        int16_t sample;
        std::memcpy(&sample,pcm+i,sizeof(sample));
        sample=static_cast<int16_t>(static_cast<int>(sample)*percent/100);
        std::memcpy(pcm+i,&sample,sizeof(sample));
    }
}
}

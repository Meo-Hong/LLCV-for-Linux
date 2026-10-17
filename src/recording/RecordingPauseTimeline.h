#pragma once
#include <algorithm>
#include <cstdint>

namespace llcv::recording {
// Only the recording worker changes this mapping. Both streams share a cut;
// paused input is intentionally omitted, never represented by frozen footage.
class RecordingPauseTimeline {
    uint64_t generation_=0;
    int64_t offset_=-1,resumeSource_=-1,videoEnd_=0,audioEnd_=0;
public:
    bool NeedsVideo(uint64_t generation) const { return generation>generation_; }
    struct Mapped { int64_t time=0,duration=0;uint32_t skipAudioFrames=0;bool keep=false; };
    Mapped Map(bool video,uint64_t generation,int64_t time,int64_t duration) {
        if(duration<=0 || time<0 || (generation&1) || generation<generation_)return {};
        if(generation!=generation_){
            if(!video)return {};
            generation_=generation;resumeSource_=time;
            offset_=time-std::max(videoEnd_,audioEnd_);
        }
        if(offset_<0 && resumeSource_<0)offset_=time;
        uint32_t skip=0;
        if(!video && resumeSource_>=0){
            const auto first=std::max(resumeSource_,audioEnd_+offset_);
            if(time<first){
                skip=static_cast<uint32_t>(((first-time)*48000+9999999)/10000000);
                const auto removed=int64_t(skip)*10000000/48000;
                time+=removed;duration-=removed;
            }
        }
        if(time<offset_ || duration<=0)return {};
        const auto out=time-offset_;
        auto& end=video?videoEnd_:audioEnd_;
        end=std::max(end,out+duration);
        return {out,duration,skip,true};
    }
};
}

#pragma once
#include <cstddef>
#include <cstdint>
namespace llcv::recording {
// MF clean points can be HEVC CRA pictures with leading RASL frames. Starting a
// new decoder there silently discards those pictures. Reordered HEVC must
// have an IDR; the mux also checks its lookahead for leading pictures.
inline bool HasHevcIdr(const uint8_t* data,size_t size){
    for(size_t i=0;i+4<size;++i){
        if(data[i]==0&&data[i+1]==0&&data[i+2]==1)
            if(((data[i+3]>>1)&63)==19||((data[i+3]>>1)&63)==20)return true;
    }
    // Some MFTs provide 4-byte length-prefixed NAL units instead of Annex B.
    for(size_t i=0;i+4<size;){
        const uint32_t length=(uint32_t(data[i])<<24)|(uint32_t(data[i+1])<<16)|(uint32_t(data[i+2])<<8)|data[i+3];
        i+=4;if(length<2||length>size-i)break;
        if(((data[i]>>1)&63)==19||((data[i]>>1)&63)==20)return true;
        i+=length;
    }
    return false;
}
}

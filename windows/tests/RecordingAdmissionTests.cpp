#include "recording/RecordingAdmission.h"
#include <cstdio>
#include <initializer_list>
#include <thread>

using namespace llcv::recording;
namespace {
int failures=0;
void Expect(bool passed,const char* message) {
    if(!passed){++failures;std::printf("FAIL: %s\n",message);}
}
}
int main() {
    std::atomic<Phase> phase{Phase::Starting};
    BeginFinalizing(phase);
    Expect(!TryPublishRecording(phase) && phase==Phase::Finalizing,
        "stop before publication cannot reopen capture admission");
    phase=Phase::Starting;
    Expect(TryPublishRecording(phase),"ready worker publishes recording");
    BeginFinalizing(phase);
    Expect(!TryPublishRecording(phase) && phase==Phase::Finalizing,
        "stop after publication closes admission permanently");
    for(auto terminal:{Phase::Idle,Phase::Saved,Phase::Failed}){
        phase=terminal;BeginFinalizing(phase);
        Expect(!TryPublishRecording(phase) && phase==terminal,
            "stop and late publication preserve idle and terminal states");
    }
    // Run both production transitions concurrently, without an encoder or
    // scheduler-dependent sleep. Each round ends only after both have returned.
    constexpr unsigned rounds=10000;
    std::atomic<unsigned> start{0},published{0},stopped{0};
    std::thread worker([&]{
        for(unsigned round=1;round<=rounds;++round){
            while(start.load(std::memory_order_acquire)!=round)std::this_thread::yield();
            TryPublishRecording(phase);
            published.store(round,std::memory_order_release);
        }
    });
    std::thread stop([&]{
        for(unsigned round=1;round<=rounds;++round){
            while(start.load(std::memory_order_acquire)!=round)std::this_thread::yield();
            BeginFinalizing(phase);
            stopped.store(round,std::memory_order_release);
        }
    });
    for(unsigned round=1;round<=rounds;++round){
        phase=Phase::Starting;
        start.store(round,std::memory_order_release);
        while(published.load(std::memory_order_acquire)!=round ||
              stopped.load(std::memory_order_acquire)!=round)std::this_thread::yield();
        Expect(phase==Phase::Finalizing,"concurrent publication cannot undo a completed stop");
    }
    worker.join();stop.join();
    std::atomic<bool> stopping{true};std::atomic<unsigned> producers{0};
    std::atomic<size_t> videoWrite{0},audioWrite{0};
    phase=Phase::Recording;
    Expect(!ReadyToFinalizeRecording(stopping,phase,producers,0,videoWrite,0,audioWrite),
        "stop flag alone cannot finalize while capture admission is open");
    phase=Phase::Finalizing;producers=1;
    Expect(!ReadyToFinalizeRecording(stopping,phase,producers,0,videoWrite,0,audioWrite),
        "active callback prevents finalization");
    producers=0;stopping=false;
    Expect(!ReadyToFinalizeRecording(stopping,phase,producers,0,videoWrite,0,audioWrite),
        "idle recording cannot finalize without stop");
    stopping=true;
    for(bool video:{true,false}){
        videoWrite=0;audioWrite=0;producers=1;
        std::atomic<unsigned> gate{0};
        std::thread producer([&]{
            while(gate.load(std::memory_order_acquire)!=1)std::this_thread::yield();
            (video?videoWrite:audioWrite).store(1,std::memory_order_release);
            producers.store(0,std::memory_order_release);
            gate.store(2,std::memory_order_release);
        });
        const bool oldEmpty=videoWrite.load(std::memory_order_acquire)==0 && audioWrite.load(std::memory_order_acquire)==0;
        gate.store(1,std::memory_order_release);
        while(gate.load(std::memory_order_acquire)!=2)std::this_thread::yield();
        Expect(oldEmpty && producers==0,"reproduce empty snapshot followed by final callback publication");
        Expect(!ReadyToFinalizeRecording(stopping,phase,producers,0,videoWrite,0,audioWrite),
            "last audio/video callback is drained despite stale empty snapshot");
        Expect(ReadyToFinalizeRecording(stopping,phase,producers,video?1:0,videoWrite,video?0:1,audioWrite),
            "finalization proceeds after both fresh queue tails are drained");
        producer.join();
    }
    std::printf("Recording admission: %u concurrent publication/stop rounds, failures=%d\n",rounds,failures);
    return failures?1:0;
}

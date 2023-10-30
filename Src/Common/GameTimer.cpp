// Prevent definition of min and max macros.
#define NOMINMAX   
#include <windows.h>
#include "GameTimer.h"

GameTimer::GameTimer()
  : mSecondsPerCount(0.0),
    mDeltaTime(-1.0),
    mBaseTime(0),
    mPausedTime(0),
    mPrevTime(0),
    mCurrTime(0),
    mStopped(false) {

  __int64 countsPerSec;
  // From <profileapi.h>. The performance counter is a high resolution (<1 micro second)
  // timestamp that can be used for time-interval measurements. The performance counter 
  // frequency is the number of times it gets incremented in a second (counts per second).
  QueryPerformanceFrequency((LARGE_INTEGER*)&countsPerSec);
  mSecondsPerCount = 1.0 / (double)countsPerSec;
}

float GameTimer::TotalTime() const {
  if (mStopped) {
    return (float)(((mStopTime - mPausedTime) - mBaseTime) * mSecondsPerCount);
  } else {
    return (float)(((mCurrTime - mPausedTime) - mBaseTime) * mSecondsPerCount);
  }
}

float GameTimer::DeltaTime() const {
  return (float)mDeltaTime;
}

void GameTimer::Reset() {
  __int64 currTime;
  QueryPerformanceCounter((LARGE_INTEGER*)&currTime);
  // TODO: base time vs start time?
  mBaseTime = currTime;
  mPrevTime = currTime;
  // Previous stop time doesn't matter anymore.
  mStopTime = 0;
  mStopped = false;
}

void GameTimer::Start() {
  __int64 startTime;
  QueryPerformanceCounter((LARGE_INTEGER*)&startTime);
  if (mStopped) {
    mPausedTime += (startTime - mStopTime);
    mPrevTime = startTime;
    mStopTime = 0;
    mStopped = false;
  }
}

void GameTimer::Stop() {
  if (!mStopped) {
    __int64 currTime;
    QueryPerformanceCounter((LARGE_INTEGER*)&currTime);

    mStopTime = currTime;
    mStopped = true;
  }
}

void GameTimer::Tick() {
  if (mStopped) {
    mDeltaTime = 0.0;
    return;
  }

  __int64 currTime;
  QueryPerformanceCounter((LARGE_INTEGER*)&currTime);
  mCurrTime = currTime;

  mDeltaTime = (mCurrTime - mPrevTime) * mSecondsPerCount;

  mPrevTime = mCurrTime;

  if (mDeltaTime < 0.0) {
    mDeltaTime = 0.0;
  }
}
#ifndef GAMETIMER_H
#define GAMETIMER_H

class GameTimer {
private:
  double mSecondsPerCount;

  // Time elapsed since the last tick, in seconds.
  double mDeltaTime;

  __int64 mBaseTime;
  // Accumulates the time spent stopped (the timer may be stopped in various
  // noncontiguous intervals); mPausedTime is the sum of the extent of those 
  // intervals.
  __int64 mPausedTime;
  // Timestamp (given by the performance counter) of the last time the timer
  // was stopped.
  __int64 mStopTime;
  // Timestamp of the previous tick.
  __int64 mPrevTime;
  __int64 mCurrTime;

  bool mStopped;

public:
  GameTimer();

  // Total time elapsed since the last reset, excluding the time spent stopped.
  float TotalTime() const;
  float DeltaTime() const;
  void Reset();
  void Start();
  void Stop();
  void Tick();
};

#endif // GAMETIMER_H
#pragma once

// Small PID controller.
//  - Derivative acts on the measurement, so setpoint changes don't cause a kick.
//  - Integral is clamped so the total output can't wind up past its limits.
class Pid {
 public:
  void setGains(float kp, float ki, float kd) {
    kp_ = kp;
    ki_ = ki;
    kd_ = kd;
  }
  void setOutputLimits(float lo, float hi) {
    lo_ = lo;
    hi_ = hi;
  }
  void reset() {
    integral_ = 0;
    primed_ = false;
  }

  // dt in seconds. Returns the clamped output.
  float update(float setpoint, float measured, float dt) {
    if (dt <= 0) return last_;
    const float err = setpoint - measured;
    const float p = kp_ * err;
    float d = 0;
    if (primed_) d = -kd_ * (measured - prevMeasured_) / dt;
    prevMeasured_ = measured;
    primed_ = true;

    integral_ += ki_ * err * dt;
    // Anti-windup: keep the integral term within what's useful for the output.
    const float iMax = hi_ - (p + d), iMin = lo_ - (p + d);
    if (integral_ > iMax) integral_ = iMax > 0 ? iMax : 0;
    if (integral_ < iMin) integral_ = iMin < 0 ? iMin : 0;

    float out = p + integral_ + d;
    if (out > hi_) out = hi_;
    if (out < lo_) out = lo_;
    last_ = out;
    return out;
  }

 private:
  float kp_ = 0, ki_ = 0, kd_ = 0;
  float lo_ = 0, hi_ = 100;
  float integral_ = 0, prevMeasured_ = 0, last_ = 0;
  bool primed_ = false;
};

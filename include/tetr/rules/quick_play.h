#ifndef TETR_RULES_QUICK_PLAY_H
#define TETR_RULES_QUICK_PLAY_H

#include "tetr/runtime/quick_play_progress.h"
#include "tetr/runtime/runtime_config.h"

namespace Rules {

int qp_floor(double altitude);
int qp_snowball_size(int clearedLines);
double qp_floor_altitude(int floor);
double qp_survival_apm(double altitude);
bool qp_hard_mode(const Runtime::QuickPlayConfig& config);
Runtime::Duration qp_garbage_delay(const Runtime::QuickPlayConfig& config, int floor, double cancellation);
Runtime::Duration qp_garbage_rise_interval(const Runtime::QuickPlayConfig& config, Runtime::Duration configured);
double qp_messiness(const Runtime::QuickPlayConfig& config, int floor,
                   const Runtime::QuickPlayProgress& progress, double elapsed);
double qp_garbage_favor(const Runtime::QuickPlayConfig& config, int floor);
double qp_receive_attack(double attack, double altitude, double attackerAltitude,
                         int floor, const Runtime::QuickPlayConfig& config,
                         const Runtime::QuickPlayProgress& progress, double elapsed);
std::vector<int> qp_windup_segments(int attack, double altitude, bool volatileMode);
// Receives an already adjusted attack. Large attacks are clipped and their
// warning/segments are serialized behind the previous Windup's ending stage.
bool qp_queue_windup(Runtime::QuickPlayProgress& progress, int attack,
                     double altitude, bool volatileMode, double elapsed);
void qp_relieve_cancellation(Runtime::QuickPlayProgress& progress, double amount);
void qp_record_placement(Runtime::QuickPlayProgress& progress, const Runtime::QuickPlayConfig& config,
                         int lines, int garbageLines,
                         bool perfectClear, bool spin, int family, int column, int cancelled, bool stackedEdgeI=false);
void qp_reward_placement(Runtime::QuickPlayProgress& progress, double level, double altitude,
                         int outgoing, int cancelled, int lines, bool spin, int dugLines,
                         const Runtime::QuickPlayConfig& config, double experienceMultiplier = 1.0);
void qp_advance_booster(Runtime::QuickPlayProgress& progress, double& level,
                        double& altitude, int floor, const Runtime::QuickPlayConfig& config, double deltaSeconds);

} // namespace Rules
#endif

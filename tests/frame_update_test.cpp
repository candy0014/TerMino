#include "tetr/compat/legacy_encoding.h"
#include "tetr/rules/frame_update.h"
#include "tetr/network/frame_packet.h"
#include "tetr/network/frame_pump.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <deque>
#include <iostream>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

struct LegacyPreResult {
    std::vector<int> packets;
};

LegacyPreResult legacy_pre(
    Rules::FrameRuleState& state,
    const Rules::FrameRuleConfig& config,
    double dtime,
    std::mt19937& rng) {
    LegacyPreResult result;
    const int garbageMode = Compat::to_legacy(config.garbageMode);
    const int expertMode = Compat::to_legacy(config.qpModExpert);
    if (garbageMode == 5) {
        if (!config.replayMode) {
            double q = dtime;
            while (q > 1e-7) {
                double dt = std::min(0.05, q);
                q -= dt;
                if (rng() % 3600 < state.survivalApm * dt / 0.05) {
                    int atk = rng() % 5 + 1;
                    result.packets.push_back(atk);
                }
            }
        }
    }
    if (garbageMode == 10) {
        if (!config.replayMode) {
            double sa = state.survivalApm * config.qpGarbageMultiple + 5;
            double q = dtime;
            double tim = config.elapsedGameTime.seconds();
            if (expertMode == 2) tim *= 2;
            double apm = sa;
            if (tim >= 300) apm *= std::pow(2 + expertMode, (tim - 300) / 300);
            double windup = 0.1 * apm / 60 * apm;
            if (apm <= 60) apm -= windup;
            else {
                double tapm = 0;
                double p = 0.9;
                windup = 0;
                for (int i = 0; i < apm; i += 60) {
                    double segment = apm - i;
                    if (segment > 60) segment = 60;
                    tapm += segment * p;
                    windup += segment * (1 - p);
                    p *= 0.9;
                }
                apm = tapm;
            }
            long long a1 = apm * config.qpGarbageMultiple;
            long long a2 = windup * config.qpGarbageMultiple;
            (void)a1;
            (void)a2;
            long long o = (int)(std::pow(sa, 0.7) / 8) + 0.5;
            if (o == 0) o = 1;
            int sapm = 5;
            if (apm < 5) sapm = apm;
            apm -= sapm;
            while (q > 1e-7) {
                double dt = std::min(0.05, q);
                q -= dt;
                if (rng() % 1200 < sapm * dt / 0.05) {
                    result.packets.push_back(1);
                }
                if (rng() % (1200 * 3) < apm * dt / 0.05) {
                    int atk = rng() % 5 + 1;
                    result.packets.push_back(atk);
                }
                if (rng() % (1200 * (4 * o + 2)) < windup * dt / 0.05) {
                    int atk = rng() % 5;
                    for (int i = 0; i < o; i++) result.packets.push_back(4);
                    if (atk) result.packets.push_back(atk);
                }
            }
        }
        state.qpAltitude += state.qpSpeed * dtime * 0.25;
        state.qpSpeed -= dtime * state.qpSpeed * 0.25 / 36 * std::pow(2, expertMode);
        state.survivalApm = std::pow(std::max(0.0, state.qpAltitude), 1.1) * 0.01 + 5;
    }
    return result;
}

bool legacy_post(
    Rules::FrameRuleState& state,
    const Rules::FrameRuleConfig& config,
    double dtime) {
    const int garbageMode = Compat::to_legacy(config.garbageMode);
    const int expertMode = Compat::to_legacy(config.qpModExpert);
    const int gravityMode = Compat::to_legacy(config.qpModGravity);
    double floors[15] = {-1e-7,50,150,300,450,650,850,1100,1350,1650};
    if (garbageMode != 10) return false;
    int lastFloor = state.qpFloor;
    state.qpFloor = std::lower_bound(floors, floors + 10, state.qpAltitude + 1e-7) - floors;
    state.garbageInTime = config.qpGarbageInTime * ((11 - state.qpFloor) * 0.5);
    if (lastFloor != state.qpFloor) {
        state.qpBagBreather = 1;
        if (gravityMode) {
            double gravity = Compat::to_legacy_seconds(state.gravitySpeed);
            if (gravity > 1e-8) gravity = 1 / (1 / gravity + 18);
            if (gravity * 1200 < 1) gravity = 0;
            state.gravitySpeed = Compat::gravity_from_legacy_seconds(gravity);
            if (gravityMode == 1) {
                int vals[15] = {0,30,29,28,27,26,24,22,20,18,16};
                state.epld = Runtime::Duration::from_seconds(vals[state.qpFloor] / 60.0);
            } else {
                int vals[15] = {0,24,22,20,18,16,15,14,13,12,11};
                state.epld = Runtime::Duration::from_seconds(vals[state.qpFloor] / 60.0);
            }
        }
    }
    if (expertMode == 2) {
        state.qpAltitude = std::max(
            state.qpAltitude - dtime * state.qpFloor,
            floors[state.qpFloor - 1]);
    }
    return lastFloor != state.qpFloor;
}

void assert_close(double a, double b) {
    const double scale = std::max({1.0, std::fabs(a), std::fabs(b)});
    assert(std::fabs(a - b) <= 1e-12 * scale);
}

void assert_state_equal(const Rules::FrameRuleState& a, const Rules::FrameRuleState& b) {
    assert_close(a.qpSpeed, b.qpSpeed);
    assert_close(a.qpAltitude, b.qpAltitude);
    assert(a.qpFloor == b.qpFloor);
    assert_close(a.survivalApm, b.survivalApm);
    assert_close(a.garbageInTime.seconds(), b.garbageInTime.seconds());
    assert_close(Compat::to_legacy_seconds(a.gravitySpeed), Compat::to_legacy_seconds(b.gravitySpeed));
    assert_close(a.epld.seconds(), b.epld.seconds());
    assert(a.qpBagBreather == b.qpBagBreather);
}

void test_frame_rules_random() {
    std::mt19937 gen(0x51A9E123u);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    for (int tc = 0; tc < 100000; ++tc) {
        Rules::FrameRuleState a;
        a.qpSpeed = unit(gen) * 800;
        a.qpAltitude = unit(gen) * 1900;
        a.qpFloor = 1 + gen() % 9;
        a.survivalApm = unit(gen) * 800;
        a.garbageInTime = Runtime::Duration::from_seconds(unit(gen) * 6);
        a.gravitySpeed = Compat::gravity_from_legacy_seconds(unit(gen) < 0.1 ? 0.0 : std::pow(10.0, -5.0 + unit(gen) * 5.0));
        a.epld = Runtime::Duration::from_seconds(unit(gen));
        a.qpBagBreather = (gen() % 3) != 0;
        Rules::FrameRuleState b = a;

        Rules::FrameRuleConfig config;
        const int models[] = {0, 1, 5, 11};
        config.garbageMode = Compat::garbage_mode_from_legacy(models[gen() % 4]);
        config.replayMode = gen() & 1;
        config.qpModExpert = Compat::modifier_mode_from_legacy(gen() % 3);
        config.qpModGravity = Compat::modifier_mode_from_legacy(gen() % 3);
        config.qpGarbageMultiple = unit(gen) * 3.0;
        config.qpGarbageInTime = Runtime::Duration::from_seconds(unit(gen) * 2.0);
        config.elapsedGameTime = Runtime::Duration::from_seconds(unit(gen) * 1200);
        const double dtime = unit(gen) * 0.35;

        const std::uint32_t seed = gen();
        std::mt19937 rngA(seed), rngB(seed);
        const auto oldPre = legacy_pre(a, config, dtime, rngA);
        const auto newPre = Rules::advance_frame_before_presentation(b, config, Runtime::Duration::from_seconds(dtime), rngB);
        assert(oldPre.packets == newPre.incomingGarbagePackets);
        assert_state_equal(a, b);
        for (int i = 0; i < 8; ++i) assert(rngA() == rngB());

        legacy_post(a, config, dtime);
        Rules::advance_frame_after_presentation(b, config, Runtime::Duration::from_seconds(dtime));
        assert_state_equal(a, b);
    }
}

struct LegacyNetworkResult {
    Network::MultiplayerFrameSignal signal = Network::MultiplayerFrameSignal::Continue;
    int attack = 0;
    std::string opponentState;
};

LegacyNetworkResult legacy_network_apply(
    const std::string& s,
    std::deque<std::pair<int,int>>& sent,
    int& receiveId,
    int& wins) {
    LegacyNetworkResult result;
    if (s == "end1" || s == "end2") {
        ++wins;
        result.signal = s == "end2"
            ? Network::MultiplayerFrameSignal::OpponentLeft
            : Network::MultiplayerFrameSignal::Won;
        return result;
    }
    if (!s.empty() && s[0] == '!') {
        result.opponentState = s;
        return result;
    }
    int receive = 0, send = 0, attack = 0, flag = 0;
    for (char ch : s) {
        if (ch == ';') { ++flag; continue; }
        if (flag == 0) receive = receive * 10 + ch - '0';
        if (flag == 1) send = send * 10 + ch - '0';
        if (flag == 2) attack = attack * 10 + ch - '0';
    }
    while (!sent.empty() && sent.front().first <= send) sent.pop_front();
    while (!sent.empty() && attack) {
        int t = std::min(sent.front().second, attack);
        attack -= t;
        sent.front().second -= t;
        if (!sent.front().second) sent.pop_front();
    }
    receiveId = receive;
    result.attack = attack;
    return result;
}

void test_network_random() {
    std::mt19937 rng(0xAA771234u);
    for (int tc = 0; tc < 50000; ++tc) {
        std::deque<std::pair<int,int>> a;
        std::deque<Runtime::PendingAttack> b;
        int id = 0;
        const int n = rng() % 12;
        for (int i = 0; i < n; ++i) {
            id += 1 + rng() % 3;
            a.push_back({id, 1 + static_cast<int>(rng() % 15)});
        }
        for (const auto& pending : a) {
            b.push_back({Runtime::MultiplayerSequenceId::from_wire(pending.first), pending.second});
        }
        int recvA = rng() % 100;
        Runtime::MultiplayerSequenceId recvB = Runtime::MultiplayerSequenceId::from_wire(recvA);
        int winsA = rng() % 10, winsB = winsA;

        std::string s;
        switch (rng() % 7) {
            case 0: s = "end1"; break;
            case 1: s = "end2"; break;
            default: {
                const int r = rng() % 1000;
                const int send = rng() % (id + 5 + 1);
                const int attack = rng() % 50;
                s = std::to_string(r) + ";" + std::to_string(send) + ";" + std::to_string(attack);
                break;
            }
        }

        const auto old = legacy_network_apply(s, a, recvA, winsA);
        const auto parsed = Network::decode_frame_packet(s, 10, 20);
        assert(parsed);
        const auto now = Network::apply_frame_packet(
            *parsed,
            Network::MultiplayerFrameStateRef{b, recvB, winsB});
        assert(old.signal == now.signal);
        assert(old.attack == now.incomingAttack);
        assert(!now.opponentState);
        assert(a.size() == b.size());
        for (std::size_t i = 0; i < a.size(); ++i) {
            assert(a[i].first == b[i].sequenceId.value());
            assert(a[i].second == b[i].attack);
        }
        assert(recvA == recvB.value());
        assert(winsA == winsB);
    }
}

void test_network_pump_order() {
    std::deque<Runtime::PendingAttack> sent{
        {Runtime::MultiplayerSequenceId::from_wire(1),4},
        {Runtime::MultiplayerSequenceId::from_wire(2),7},
        {Runtime::MultiplayerSequenceId::from_wire(4),3}
    };
    Runtime::MultiplayerSequenceId receiveId{};
    int wins = 0;

    Network::FramePacket attack1;
    attack1.type = Network::FramePacketType::Attack;
    attack1.receiveId = Runtime::MultiplayerSequenceId::from_wire(5);
    attack1.sendId = Runtime::MultiplayerSequenceId::from_wire(1);
    attack1.attack = 6;

    Network::BoardStateSnapshot snapshot;
    snapshot.field.initialize(10,20);
    snapshot.score.wins = 42;
    Network::FramePacket statePacket;
    statePacket.type = Network::FramePacketType::OpponentState;
    statePacket.opponentState = snapshot;

    Network::FramePacket attack2;
    attack2.type = Network::FramePacketType::Attack;
    attack2.receiveId = Runtime::MultiplayerSequenceId::from_wire(6);
    attack2.sendId = Runtime::MultiplayerSequenceId::from_wire(2);
    attack2.attack = 4;

    Network::FramePacket win;
    win.type = Network::FramePacketType::Win;

    Network::FramePacket unconsumed;
    unconsumed.type = Network::FramePacketType::Attack;
    unconsumed.receiveId = Runtime::MultiplayerSequenceId::from_wire(7);
    unconsumed.sendId = Runtime::MultiplayerSequenceId::from_wire(4);
    unconsumed.attack = 99;

    std::deque<Network::FramePacket> input{attack1,statePacket,attack2,win,unconsumed};
    std::vector<int> attacks;
    std::vector<int> stateWins;
    Network::FramePumpCallbacks callbacks;
    callbacks.receive = [&]() -> std::optional<Network::FramePacket> {
        if (input.empty()) return std::nullopt;
        Network::FramePacket value = input.front();
        input.pop_front();
        return value;
    };
    callbacks.addIncomingAttack = [&](int x) { attacks.push_back(x); };
    callbacks.applyOpponentState = [&](const Network::BoardStateSnapshot& value) {
        stateWins.push_back(value.score.wins);
    };

    const auto signal = Network::pump_multiplayer_frame(
        Network::MultiplayerFrameStateRef{sent, receiveId, wins}, callbacks);
    assert(signal == Network::MultiplayerFrameSignal::Won);
    assert(wins == 1);
    assert(receiveId.value() == 6);
    assert(stateWins == std::vector<int>{42});
    assert(attacks == std::vector<int>({1}));
    assert(input.size() == 1);
}

} // namespace

int main() {
    test_frame_rules_random();
    test_network_random();
    test_network_pump_order();
    std::cout << "frame update tests passed\n";
}

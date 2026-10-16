#ifndef TETR_NETWORK_ROUND_STATISTICS_H
#define TETR_NETWORK_ROUND_STATISTICS_H
#include "tetr/runtime/player_state.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace Runtime {class GameContext;struct PlayerSessionState;}
namespace Network {
struct NetworkServices;
struct RoundStatistics {
    Runtime::ScoringState score;
    double gameSeconds=0,realSeconds=0;
    bool pieceTime=false;
};
RoundStatistics capture_round_statistics(const Runtime::GameContext&,const Runtime::PlayerSessionState&);
std::string encode_round_statistics(const RoundStatistics&);
std::optional<RoundStatistics> decode_round_statistics(std::string_view);
std::uint64_t round_statistics_digest(std::string_view);
std::vector<std::string> round_statistics_parts(std::string_view);
class ResultAssembler {
public:
    std::optional<std::string> insert(std::string_view part);
    std::size_t missing_part() const;
private:
    std::uint64_t digest_=0;
    std::size_t bytes_=0;
    std::vector<std::optional<std::string>> parts_;
};
void send_round_statistics(Runtime::GameContext&,const Runtime::PlayerSessionState&);
void request_round_statistics(NetworkServices&,std::uint64_t epoch,int slot);
}
#endif

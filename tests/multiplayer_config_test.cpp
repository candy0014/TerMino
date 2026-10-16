#include "tetr/config/multiplayer_config.h"
#include "tetr/config/normalization.h"
#include "tetr/config/settings_schema.h"
#include "tetr/network/room_config_protocol.h"
#include "tetr/runtime/piece.h"
#include <cassert>
#include <iostream>
int main() {
    UserConfig::Settings host,guest;
    host.gameMode=guest.gameMode=4;
    host.fieldWidth=14; host.fieldHeight=25; host.nextCount=2; host.holdEnabled=0;
    host.hitMultiplier=3.75; host.cancelMultiplier=2.5; host.lockDelay=.25;
    host.attackMultiplier=1.75;
    host.gravityInterval=.05; host.das=.07; host.arr=0; host.dcd=.002;
    host.softDropFactor=.012; host.softDropFixed=1; host.preMove=0; host.dasPrecharge=0;
    host.ghost=0; host.squareStyle=1; host.obscuredColors=1;
    host.disable180=1; host.simulationPps=2; host.turnPieces=3;
    guest.das=.42; guest.arr=.03; guest.softDropFactor=12;
    guest.ghost=1; guest.squareStyle=0; guest.username="Guest"; guest.serverIp="guest.server";
    guest.mapId=99; guest.keyCodes=UserConfig::default_player_two_key_codes();
    host.autoplayMode=2;host.autoplayPps=15;host.autoplayWidth=600;host.autoplayHandlingSimulation=0;
    guest.autoplayMode=1;guest.autoplayPps=3.25;guest.autoplayWidth=120;guest.autoplayHandlingSimulation=1;
    auto source=Runtime::normalize_user_config(host,Runtime::GameMode::Multiplayer);
    Runtime::MapDefinition map; map.id=99; map.name="Only host has this map";
    map.width=12; map.height=20; map.rows.assign(20,std::string(12,' ')); map.rows.back()[0]='G';
    map.generator=Runtime::MapGenerator::CustomSequence;
    const auto types=Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus);
    for (int i=0;i<60000;++i) map.sequence.push_back(types[i%types.size()]);
    Runtime::apply_map_config(source,map);
    const auto wire=UserConfig::encode_multiplayer_config(host,source);
    const auto decoded=UserConfig::decode_multiplayer_config(wire);
    assert(decoded && decoded->map && *decoded->map==map);
    assert(!UserConfig::decode_multiplayer_config(wire.substr(0,wire.size()-3)));
    auto invalid=wire; const auto position=invalid.find("HANDLING/DAS="); assert(position!=std::string::npos);
    invalid.replace(position,invalid.find('\n',position)-position,"HANDLING/DAS=nan");
    assert(!UserConfig::decode_multiplayer_config(invalid));
    assert(!UserConfig::decode_multiplayer_config("TETR_ROOM_CONFIG 1\nMAP\n"));
    const auto local=Runtime::normalize_user_config(guest,Runtime::GameMode::Multiplayer);
    for (bool rules:{false,true}) {
        const auto applied=UserConfig::apply_multiplayer_config(guest,local,*decoded,rules);
        assert(applied.handling.das.seconds()==guest.das);
        assert(applied.autoplay.mode==local.autoplay.mode && applied.autoplay.pps==local.autoplay.pps);
        assert(applied.autoplay.searchWidth==local.autoplay.searchWidth && applied.autoplay.handlingSimulation==local.autoplay.handlingSimulation);
        assert(applied.handling.arr.seconds()==guest.arr);
        assert(applied.handling.softDropFactor==guest.softDropFactor);
        assert(applied.handling.softDropFixed==false);
        assert(applied.handling.gravityInterval.interval().seconds()==(rules?host.gravityInterval:guest.gravityInterval));
        assert(applied.handling.epld.seconds()==(rules?host.lockDelay:guest.lockDelay));
        assert(applied.garbage.hitMultiplier==(rules?host.hitMultiplier:guest.hitMultiplier));
        assert(applied.garbage.sendMultiplier==(rules?host.attackMultiplier:guest.attackMultiplier));
        assert(applied.field.width==(rules?map.width:guest.fieldWidth));
        assert(applied.field.nextCount==(rules?host.nextCount:guest.nextCount));
        assert(applied.field.ghost && applied.display.squareStyle==guest.squareStyle && !applied.display.obscuredColors);
        if (rules) {assert(applied.initialMap && *applied.initialMap==map); assert(applied.initialMap->sequence.size()==60000);}
        else assert(!applied.initialMap);
    }
    assert(guest.mapId==99 && guest.username=="Guest" && guest.serverIp=="guest.server" && guest.das==.42);
    assert(guest.keyCodes==UserConfig::default_player_two_key_codes());
    const auto preview=UserConfig::multiplayer_config_view_settings(guest,*decoded);
    assert(preview.fieldWidth==host.fieldWidth && preview.autoplayMode==guest.autoplayMode);
    assert(preview.autoplayPps==guest.autoplayPps && preview.autoplayWidth==guest.autoplayWidth && preview.autoplayHandlingSimulation==guest.autoplayHandlingSimulation);
    auto copied=guest;auto noMap=*decoded;noMap.map.reset();
    assert(UserConfig::copy_multiplayer_config(copied,noMap));
    assert(copied.fieldWidth==host.fieldWidth && copied.autoplayMode==guest.autoplayMode);
    assert(copied.autoplayPps==guest.autoplayPps && copied.autoplayWidth==guest.autoplayWidth && copied.autoplayHandlingSimulation==guest.autoplayHandlingSimulation);
    // Missing, reordered, duplicated and damaged fragments cannot yield a partial configuration.
    Network::ConfigAssembler assembler;
    const auto digest=Network::room_config_digest(wire);
    const auto count=Network::room_config_chunk_count(wire.size()); assert(count>100);
    for (std::size_t i=count;i-->1;) {
        const auto part=Network::room_config_part(wire,"token",2,digest,i);
        const auto packet=Network::decode_config_message(Network::encode_config_message(part));
        assert(packet && assembler.insert(*packet) && assembler.insert(*packet));
    }
    assert(!assembler.payload);
    auto first=Network::room_config_part(wire,"token",2,digest,0);
    auto damaged=first; damaged.data[0]^=1;
    assert(!assembler.insert(damaged) && !Network::decode_config_message(Network::encode_config_message(damaged)));
    assert(assembler.insert(first) && assembler.payload==wire);
    assert(!Network::decode_config_message("!config-part;token;1;3100001;1;0;0;x"));
    std::string binary(2500,';'); binary[100]='\0';
    Network::ConfigAssembler binaryAssembler;
    for (std::size_t i=0;i<Network::room_config_chunk_count(binary.size());++i) {
        const auto packet=Network::decode_config_message(Network::encode_config_message(Network::room_config_part(binary,"token",3,Network::room_config_digest(binary),i)));
        assert(packet && binaryAssembler.insert(*packet));
    }
    assert(binaryAssembler.payload==binary);
    UserConfig::Settings reset; reset.gameMode=4; reset.serverIp="wss://custom/ws"; reset.username="Kept user";
    reset.fieldWidth=19;
    UserConfig::reset_section(reset,UserConfig::Section::Model);
    assert(reset.gameMode==4);
    UserConfig::reset_settings(reset);
    assert(reset.gameMode==4 && reset.serverIp=="wss://custom/ws" && reset.username=="Kept user" && reset.fieldWidth==10);
    std::cerr<<"multiplayer_config_test: OK; 60000-piece map="<<wire.size()<<" bytes in "<<count<<" fragments\n";
}

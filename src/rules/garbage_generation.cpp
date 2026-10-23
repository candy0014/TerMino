#include "tetr/rules/garbage_generation.h"
#include "tetr/util/deterministic_random.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace Rules {
namespace {

bool chance(std::mt19937& rng, double probability) {
    return rng() % 1000000 < 1000000 * probability;
}

bool uses_batch_hole_shuffle(Runtime::GarbageMode garbageMode) {
    return garbageMode == Runtime::GarbageMode::Backfire ||
           garbageMode == Runtime::GarbageMode::Unclear ||
           garbageMode == Runtime::GarbageMode::Survival ||
           garbageMode == Runtime::GarbageMode::Multiplayer ||
           garbageMode == Runtime::GarbageMode::QuickPlay;
}

bool uses_standard_row_generation(const GarbageGenerationConfig& config) {
    if (config.legacyShapeRules)
        return !Runtime::is_cheese_mode(config.garbageMode) || std::abs(config.cheeseModel) == 1;
    return Runtime::enabled(config.doubleHoleMode) || config.reverseNoHold ||
        std::abs(config.cheeseModel) == 1;
}

void apply_special_hole_shape(const GarbageGenerationConfig& config, GarbageGenerationState& state) {
    if (config.legacyShapeRules) return;
    if (config.reverseNoHold)
        state.holePattern=Runtime::GarbageHolePattern::contiguous(std::min(2,config.width));
    else if (Runtime::enabled(config.doubleHoleMode))
        state.holePattern=Runtime::GarbageHolePattern::scattered(1);
}

std::pair<int,int> reverse_gray_range(int width) {
    // Integer arithmetic gives exact floor(30%) and ceil(40%). Keep at least
    // one hole even on a custom one-column field.
    return {width*3/10,std::min(width-1,(width*4+9)/10)};
}

int added_reverse_gray_cells(int width, int existing, int added, std::mt19937& rng) {
    const auto [minimum,maximum]=reverse_gray_range(width);
    const int minimumAdded=std::clamp(minimum-existing,0,added);
    const int maximumAdded=std::clamp(maximum-existing,0,added);
    if (minimumAdded==maximumAdded) return minimumAdded;
    return minimumAdded+static_cast<int>(rng()%(maximumAdded-minimumAdded+1));
}

bool valid_hole_order(const std::vector<int>& order, int width) {
    if (width <= 0 || static_cast<int>(order.size()) != width) return false;
    std::vector<bool> seen(static_cast<std::size_t>(width), false);
    for (int i = 0; i < width; ++i) {
        const int value = order[static_cast<std::size_t>(i)];
        if (value < 0 || value >= width || seen[static_cast<std::size_t>(value)]) return false;
        seen[static_cast<std::size_t>(value)] = true;
    }
    return true;
}

void repair_hole_order(GarbageGenerationState& state, int width) {
    if (valid_hole_order(state.holeOrder, width)) return;
    state.holeOrder.resize(static_cast<std::size_t>(std::max(0, width)));
    std::iota(state.holeOrder.begin(), state.holeOrder.end(), 0);
}

void avoid_previous_column(const GarbageGenerationConfig& config, GarbageGenerationState& state, std::mt19937& rng) {
    if (!config.avoidSameColumn || state.holeOrder.size()<2) return;
    const int maximum = config.width - (state.holePattern.is_contiguous() ? state.holePattern.extent() : 1);
    const auto first = std::find_if(state.holeOrder.begin(),state.holeOrder.end(),[&](int col){return col<=maximum;});
    if (first==state.holeOrder.end() || *first!=state.lastColumn) return;
    std::vector<std::size_t> alternatives;
    for(std::size_t i=0;i<state.holeOrder.size();++i)
        if(state.holeOrder[i]<=maximum && state.holeOrder[i]!=state.lastColumn) alternatives.push_back(i);
    if(!alternatives.empty()) std::iter_swap(first,state.holeOrder.begin()+alternatives[rng()%alternatives.size()]);
}

int qp_margin(const GarbageGenerationConfig& config) {
    if(!config.concentrate)return 0;
    return config.legacyQuickPlayRules ? (config.width>4?2:0) :
        std::min((config.width-1)/2,static_cast<int>(std::floor(config.width/5.0+.5)));
}

void choose_qp_hole(const GarbageGenerationConfig& config, GarbageGenerationState& state, std::mt19937& rng) {
    std::vector<int> columns(config.width);
    std::iota(columns.begin(), columns.end(), 0);
    Util::deterministic_shuffle(columns, rng);
    if (config.digScores.size() == columns.size()) std::stable_sort(columns.begin(), columns.end(),
        [&](int a, int b) { return config.digScores[a] < config.digScores[b]; });
    std::vector<double> weights(config.width);
    double total = 0;
    const int margin = qp_margin(config);
    for (int rank = 0; rank < config.width; ++rank) {
        const int column = columns[rank];
        if (column < margin || column >= config.width - margin) continue;
        weights[rank] = std::max(0.0, 10 + config.favor * (1 - 2.0 * rank / std::max(1, config.width - 1)));
        total += weights[rank];
    }
    // Concentration can exclude every positive favored rank on a custom field.
    if (total <= 0) for (int rank = 0; rank < config.width; ++rank) {
        const int column = columns[rank];
        if (column >= margin && column < config.width - margin) { weights[rank] = 1; ++total; }
    }
    double roll = static_cast<double>(rng()) / (static_cast<double>(std::mt19937::max()) + 1) * total;
    int chosen = columns.back();
    for (int rank = 0; rank < config.width; ++rank) {
        roll -= weights[rank];
        if (roll < 0) { chosen = columns[rank]; break; }
    }
    const auto position = std::find(state.holeOrder.begin(), state.holeOrder.end(), chosen);
    std::iter_swap(state.holeOrder.begin(), position);
}

} // namespace

std::vector<double> qp_garbage_dig_scores(int width,int firstRow,int bottom,
    const std::function<Runtime::BoardCell(int,int)>& cell,bool legacy) {
    std::vector<double> scores;
    if(legacy) {
        int top=bottom;std::vector<int> holes;
        for(int y=firstRow;y<bottom;++y) {
            bool garbage=false;
            for(int x=0;x<width;++x) {
                if(cell(y,x).occupied())top=std::min(top,y);
                garbage|=cell(y,x).is_normal_garbage();
            }
            if(garbage && holes.empty())for(int x=0;x<width;++x)
                if(cell(y,x).empty_cell())holes.push_back(x);
        }
        for(int x=0;x<width;++x) {
            int lowest=top,distance=holes.empty()?0:width;bool empty=false;
            for(int y=bottom-1;y>=top;--y)if(cell(y,x).empty_cell()){lowest=y;empty=true;break;}
            for(int hole:holes)distance=std::min(distance,std::abs(hole-x));
            scores.push_back(empty?lowest-top+5*distance:0);
        }
        return scores;
    }
    // The reviewed client chooses the first hole in the lowest incomplete
    // row (only when that row contains garbage), then ranks each skyline.
    int anchor=-1;
    for(int y=bottom-1;y>=firstRow;--y) {
        int hole=-1;bool garbage=false;
        for(int x=0;x<width;++x) {
            if(hole<0 && cell(y,x).empty_cell())hole=x;
            garbage|=cell(y,x).is_normal_garbage();
        }
        if(hole>=0) {if(garbage)anchor=hole;break;}
    }
    for(int x=0;x<width;++x) {
        double score=0;
        for(int y=firstRow;y<bottom;++y)if(cell(y,x).occupied()) {
            score=bottom-y+(anchor<0?0:5*std::abs(x-anchor));break;
        }
        scores.push_back(score);
    }
    return scores;
}

void prepare_garbage_batch(
    const GarbageGenerationConfig& config,
    GarbageGenerationState& state,
    std::mt19937& rng,
    bool preserveHole) {
    apply_special_hole_shape(config,state);
    repair_hole_order(state, config.width);
    if (config.qpPolicy && !preserveHole) {
        const int margin = qp_margin(config);
        if ((!config.legacyQuickPlayRules && state.lastColumn<0) ||
            state.holeOrder[0] < margin || state.holeOrder[0] >= config.width - margin ||
            chance(rng, config.continuousMessiness)) choose_qp_hole(config, state, rng);
        return;
    }
    if (uses_batch_hole_shuffle(config.garbageMode) && !preserveHole) {
        Util::deterministic_shuffle(state.holeOrder, rng);
        avoid_previous_column(config,state,rng);
    }
}

std::vector<Runtime::BoardCell> generate_garbage_row(
    const GarbageGenerationConfig& config,
    GarbageGenerationState& state,
    std::mt19937& rng) {
    if (config.width <= 0) return {};
    repair_hole_order(state, config.width);
    // Special generators own their shape independently of the shared setting.
    apply_special_hole_shape(config,state);
    if (uses_standard_row_generation(config)) {
        if (Runtime::is_cheese_mode(config.garbageMode)) {
            if (chance(rng, config.cheeseMessiness)) {
                Util::deterministic_shuffle(state.holeOrder, rng);
            }
        }
        if (chance(rng, config.singleMessiness)) {
            if (config.qpPolicy) choose_qp_hole(config, state, rng);
            else Util::deterministic_shuffle(state.holeOrder, rng);
            avoid_previous_column(config,state,rng);
        }
        if (Runtime::reverse(config.doubleHoleMode)) {
            Util::deterministic_shuffle(state.holeOrder, rng);
        }

        std::vector<Runtime::BoardCell> row(config.width,
            config.cell);
        int anchorColumn = state.holeOrder[0];
        if (Runtime::reverse(config.doubleHoleMode)) {
            const auto [minimum,maximum]=reverse_gray_range(config.width);
            const int gray=minimum+static_cast<int>(rng()%(maximum-minimum+1));
            state.holePattern=Runtime::GarbageHolePattern::scattered(config.width-gray);
            avoid_previous_column(config,state,rng);
            anchorColumn = state.holeOrder[0];
        }

        if (!state.holePattern.is_contiguous()) {
            const int count = std::min(config.width, std::max(1, state.holePattern.extent()));
            for (int j = 0; j < count; ++j) row[state.holeOrder[j]] = Runtime::BoardCell::empty();
        } else {
            const int length = std::min(config.width, state.holePattern.extent());
            for (int j = 0; j < config.width; ++j) {
                if (state.holeOrder[j] + length <= config.width) {
                    anchorColumn = state.holeOrder[j];
                    for (int k = state.holeOrder[j]; k < state.holeOrder[j] + length; ++k) {
                        row[k] = Runtime::BoardCell::empty();
                    }
                    break;
                }
            }
        }

        if (config.width > 1 && config.doubleHoleMode == Runtime::ModifierMode::Enabled && rng() % 2) {
            int extraHole;
            do {
                extraHole = static_cast<int>(rng() % static_cast<unsigned>(config.width));
            } while (extraHole == state.holeOrder[0]);
            row[extraHole] = Runtime::BoardCell::empty();
        }
        state.lastColumn = anchorColumn;
        return row;
    }

    state.chessboardParity = !state.chessboardParity;
    std::vector<int> occupied(config.width, 0);
    bool allOccupied = true;
    for (int col = 0; col < config.width; ++col) {
        if (std::abs(config.cheeseModel) == 2) {
            if (chance(rng, config.cheeseMessiness)) occupied[col] = 1;
        }
        if (std::abs(config.cheeseModel) == 3) {
            occupied[col] = (static_cast<int>(state.chessboardParity) ^ col) & 1;
        }
        if (!occupied[col]) allOccupied = false;
    }
    if (allOccupied) occupied[rng() % config.width] = 0;

    std::vector<Runtime::BoardCell> row(config.width, Runtime::BoardCell::empty());
    for (int col = 0; col < config.width; ++col) {
        row[col] = occupied[col] == 0 ? Runtime::BoardCell::empty() :
            config.legacyShapeRules ? Runtime::BoardCell::garbage() : config.cell;
    }
    return row;
}

std::vector<Runtime::BoardCell> generate_solid_garbage_row(
    int width,
    Runtime::BoardCell cell,
    Runtime::ModifierMode doubleHoleMode,
    int timedCountdown,
    GarbageGenerationState& state,
    std::mt19937& rng) {
    if (width <= 0) return {};
    repair_hole_order(state, width);
    std::vector<Runtime::BoardCell> row(width, cell);
    if (cell.kind() != Runtime::BoardCellKind::TimedSolid) return row;

    if (Runtime::reverse(doubleHoleMode)) {
        Util::deterministic_shuffle(state.holeOrder, rng);
        const auto [minimum,maximum]=reverse_gray_range(width);
        // At width 10 this retains the original 6/7 release-slot selection.
        const int holes=width-maximum+static_cast<int>(rng()%(maximum-minimum+1));
        state.holePattern = Runtime::GarbageHolePattern::scattered(holes);
        const int count = std::min(width, std::max(1, state.holePattern.extent()));
        for (int j = 0; j < count; ++j) row[state.holeOrder[j]] = Runtime::BoardCell::timed_solid(timedCountdown);
    } else {
        const int firstHole = rng() % width;
        row[firstHole] = Runtime::BoardCell::timed_solid(timedCountdown);
        if (width > 1 && doubleHoleMode == Runtime::ModifierMode::Enabled && rng() % 2) {
            int secondHole;
            do {
                secondHole = static_cast<int>(rng() % static_cast<unsigned>(width));
            } while (secondHole == firstHole);
            row[secondHole] = Runtime::BoardCell::timed_solid(timedCountdown);
        }
    }
    return row;
}

std::vector<Runtime::BoardCell> expand_garbage_row(
    const GarbageGenerationConfig& config,
    const std::vector<Runtime::BoardCell>& oldRow,
    int columnOffset,
    std::mt19937& rng) {
    const int oldWidth=static_cast<int>(oldRow.size());
    if (columnOffset<0 || config.width<oldWidth || columnOffset>config.width-oldWidth)
        throw std::out_of_range("invalid garbage row expansion");
    std::vector<Runtime::BoardCell> row(config.width,Runtime::BoardCell::empty());
    std::copy(oldRow.begin(),oldRow.end(),row.begin()+columnOffset);
    std::vector<int> addedColumns;
    for (int col=0; col<config.width; ++col)
        if (col<columnOffset || col>=columnOffset+oldWidth) addedColumns.push_back(col);
    if (addedColumns.empty()) return row;

    const auto timed=std::find_if(oldRow.begin(),oldRow.end(),[](auto cell) {
        return cell.kind()==Runtime::BoardCellKind::TimedSolid || cell.is_timed_release();
    });
    if (timed!=oldRow.end()) {
        for (int col:addedColumns) row[col]=Runtime::BoardCell::timed_solid();
        if (Runtime::reverse(config.doubleHoleMode)) {
            const auto number=std::find_if(oldRow.begin(),oldRow.end(),[](auto cell) {
                return cell.is_timed_number() || cell.is_timed_release();
            });
            if (number!=oldRow.end()) {
                // Apply the density bounds to the gray cells left after
                // release; new release slots inherit the remaining timer.
                const int existing=std::count_if(oldRow.begin(),oldRow.end(),[](auto cell) {
                    return cell.kind()==Runtime::BoardCellKind::TimedSolid && !cell.is_timed_number();
                });
                const int grayAdded=added_reverse_gray_cells(config.width,existing,
                    static_cast<int>(addedColumns.size()),rng);
                const int releases=static_cast<int>(addedColumns.size())-grayAdded;
                if (releases>0 && grayAdded>0) Util::deterministic_shuffle(addedColumns,rng);
                for (int i=0; i<releases; ++i)
                    row[addedColumns[i]]=*number;
            }
        }
        return row;
    }
    if (std::any_of(oldRow.begin(),oldRow.end(),[](auto cell) {
        return cell.kind()==Runtime::BoardCellKind::SolidGarbage;
    })) {
        for (int col:addedColumns) row[col]=Runtime::BoardCell::solid_garbage();
        return row;
    }
    const auto garbage=std::find_if(oldRow.begin(),oldRow.end(),[](auto cell) {
        return cell.is_normal_garbage();
    });
    if (garbage==oldRow.end()) return row;

    if (!uses_standard_row_generation(config)) {
        const int occupiedColumn=columnOffset+static_cast<int>(garbage-oldRow.begin());
        for (int col:addedColumns) {
            const bool occupied=std::abs(config.cheeseModel)==2 ? chance(rng,config.cheeseMessiness) :
                std::abs(config.cheeseModel)==3 && (col-occupiedColumn)%2==0;
            if (occupied) row[col]=Runtime::BoardCell::garbage();
        }
    } else if (Runtime::reverse(config.doubleHoleMode)) {
        const int existing=std::count_if(oldRow.begin(),oldRow.end(),[](auto cell) {
            return cell.is_normal_garbage();
        });
        // Choose a feasible count within the new width's density bounds,
        // filling only new columns so existing holes and pieces stay intact.
        const int grayAdded=added_reverse_gray_cells(config.width,existing,
            static_cast<int>(addedColumns.size()),rng);
        if (grayAdded>0) {
            if (grayAdded<static_cast<int>(addedColumns.size())) Util::deterministic_shuffle(addedColumns,rng);
            for (int i=0; i<grayAdded; ++i)
                row[addedColumns[i]]=Runtime::BoardCell::garbage();
        }
    } else {
        // Default, DH and rNH retain their original hole count and shape.
        for (int col:addedColumns) row[col]=Runtime::BoardCell::garbage();
    }
    return row;
}

} // namespace Rules

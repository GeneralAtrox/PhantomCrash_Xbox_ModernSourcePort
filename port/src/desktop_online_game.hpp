#pragma once
#include "desktop_coop_matches.hpp"
#include "phantomcrash/online_agreement.hpp"
#include "phantomcrash/online_battle_setup.hpp"
#include "phantomcrash/online_prediction.hpp"
#include "phantomcrash/data_roots.hpp"
#include <deque>

struct SDL_Renderer;
namespace phantomcrash {
struct OnlineBattleResources;

// Owns room-wide ordering of arena admissions. All connected peers simulate
// each arena, including peers whose local player remains in a menu.
class OnlineGame {
public:
  using Key = CoopMatches::Key;
  OnlineGame(online::Room &room, SDL_Renderer *renderer, DataRoots roots,
             std::shared_ptr<CoopMatches> matches, CoopOptions options, int robots,
             std::uint32_t input_delay = online::default_input_delay_ticks);
  void request_entry(const QuestSave &profile, const RumblingEntry &entry);
  void request_exit(const Key &key);
  bool receive(const online::RoomMessage &message, double now);
  void update(double now);
  void advance();
  [[nodiscard]] bool ready(const Key &key, int slot) const;
  [[nodiscard]] std::shared_ptr<CoopMatch> match(const Key &key) const;

private:
  enum class Phase { idle, freezing, aligning, preparing };
  struct Clock {
    std::uint64_t timeline{};
    std::uint32_t tick{};
  };
  using Clocks = std::map<Key, Clock>;
  struct Request {
    int slot{};
    online::BattleSetup setup;
    std::optional<Key> departure;
    bool synchronize{};
  };
  struct Journal {
    std::uint32_t next{};
    std::vector<std::vector<std::uint8_t>> chunks;
  };
  void collect_history();
  void remember_change(const Request &request);
  void append_history(const Key &key, std::vector<std::uint8_t> bytes);
  void begin_recovery(double now);
  bool handle_recovery(const online::Record &record, double now);
  void send_recovery();
  void replay_history(const Key &key, std::span<const std::uint8_t> bytes);
  void handle(const online::Record &record, double now);
  void begin(double now);
  void freeze(std::uint64_t transition, std::uint8_t peers, double now, std::uint8_t newcomers = 0);
  void send_report();
  void start_alignment();
  void prepare();
  void commit();
  void deliver_inputs(double now);
  void queue_exit(const Key &key, int slot);
  void find_finished();
  // Host: a player whose computer left the room leaves each arena as if by
  // Exit; their unstarted joins are dropped.
  void settle_departed();
  void apply_change(const Request &request);
  [[nodiscard]] static std::vector<std::uint8_t> encode_change(const Request &request);
  [[nodiscard]] static Request decode_change(std::span<const std::uint8_t> bytes);
  void publish(std::uint16_t kind, const std::vector<std::uint8_t> &bytes);
  [[nodiscard]] Clocks clocks() const;
  [[nodiscard]] static std::vector<std::uint8_t> encode_clocks(std::uint64_t id,
                                                               const Clocks &clocks);
  [[nodiscard]] Clocks decode_clocks(const online::Record &record) const;
  online::Room &room_;
  online::RecordChannel records_;
  online::TimelineAgreement agreement_;
  SDL_Renderer *renderer_;
  DataRoots roots_;
  std::shared_ptr<CoopMatches> matches_;
  std::shared_ptr<OnlineBattleResources> resources_;
  CoopOptions options_;
  int robots_{};
  std::uint32_t input_delay_{}; // each new match session's
  Phase phase_{Phase::idle};
  std::uint64_t transition_{}, committed_{};
  std::uint8_t peers_{}, aligned_{};
  double started_{};
  bool alignment_sent_{};
  std::array<std::optional<Clocks>, online::max_players> reports_;
  Clocks targets_;
  std::map<Key, std::uint64_t> timelines_;
  std::map<Key, std::uint32_t> verified_;
  std::deque<Request> requests_;
  std::deque<online::RoomMessage> pending_inputs_;
  std::optional<Request> current_;
  std::optional<Key> pending_local_;
  std::optional<Key> pending_exit_;
  std::uint8_t active_peers_{}, newcomers_{};
  std::map<Key, Journal> history_;
  std::map<Key, std::uint32_t> replay_ticks_;
  std::vector<std::pair<Key, std::size_t>> recovery_chunks_;
  std::array<std::size_t, online::max_players> recovery_next_{};
  std::array<bool, online::max_players> recovery_waiting_{};
  bool recovery_started_{};
};
} // namespace phantomcrash

#include "application.h"
#include "imgui.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <misc/cpp/imgui_stdlib.h>
#include <sstream>
#include <stdexcept>
#include <string>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

namespace {

void log_session_error(const std::filesystem::path &session_path,
                       const std::string &message) {
  std::cerr << "[session " << session_path.string() << "] " << message
            << std::endl;
}

} // namespace

Session::Session(Database *database, std::filesystem::path path,
                 SDL_GPUDevice *device)
    : manager(device, path), path(std::move(path)), database(database) {
#ifdef TRACY_ENABLE
  ZoneScopedN("Session::Session");
#endif
  load_existing_bill();
}

Session::~Session() {
  if (export_worker.joinable()) {
    export_worker.join();
  }
}

const std::filesystem::path &Session::session_path() const { return path; }
const std::filesystem::path &Session::image_folder() const {
  return manager.folder();
}

const std::filesystem::path *Session::current_image_path() const {
  return manager.current_image_path();
}

std::map<std::string, BillEntry> *Session::current_bill_entries() {
  const auto *image_path = current_image_path();
  if (image_path == nullptr) {
    return nullptr;
  }
  return &bill[*image_path];
}

const std::map<std::string, BillEntry> *Session::current_bill_entries() const {
  const auto *image_path = current_image_path();
  if (image_path == nullptr) {
    return nullptr;
  }

  const auto it = bill.find(*image_path);
  return it == bill.end() ? nullptr : &it->second;
}

int Session::visible_billed_entry_count() const {
  const auto *entries = current_bill_entries();
  if (entries == nullptr) {
    return 0;
  }

  int count = 0;
  for (const auto &[id, entry] : *entries) {
    (void)id;
    if (entry.count > 0) {
      ++count;
    }
  }
  return count;
}

void Session::sync_search_selection_bounds() {
  if (search_results.empty()) {
    selected_search_index = 0;
    return;
  }
  selected_search_index =
      std::clamp(selected_search_index, 0, (int)search_results.size() - 1);
}

void Session::sync_billed_selection_bounds() {
  const int entry_count = visible_billed_entry_count();
  if (entry_count == 0) {
    selected_billed_index = 0;
    return;
  }
  selected_billed_index = std::clamp(selected_billed_index, 0, entry_count - 1);
}

void Session::handle_search_keyboard_nav() {
  auto *entries = current_bill_entries();
  if (ImGui::IsKeyPressed(ImGuiKey_Tab) && entries != nullptr &&
      !entries->empty()) {
    keyboard_nav_mode = KeyboardNavMode::Billed;
    focus_billed_on_next_frame = true;
    return;
  }

  if (search_results.empty()) {
    return;
  }

  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
    selected_search_index = std::max(0, selected_search_index - 1);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
    selected_search_index =
        std::min((int)search_results.size() - 1, selected_search_index + 1);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
    const auto &line = search_results[selected_search_index];
    increment_for_id(line[0], line[1]);
  }
}

void Session::handle_billed_keyboard_nav() {
  if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
    keyboard_nav_mode = KeyboardNavMode::Search;
    focus_search_on_next_frame = true;
    return;
  }

  auto *entries = current_bill_entries();
  if (entries == nullptr || visible_billed_entry_count() == 0) {
    return;
  }

  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
    selected_billed_index = std::max(0, selected_billed_index - 1);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
    selected_billed_index =
        std::min(visible_billed_entry_count() - 1, selected_billed_index + 1);
  }

  if (!ImGui::IsKeyPressed(ImGuiKey_Enter)) {
    return;
  }

  int visible_index = 0;
  for (auto &[student_id, entry] : *entries) {
    (void)student_id;
    if (entry.count < 1) {
      continue;
    }
    if (visible_index == selected_billed_index) {
      entry.count += 1;
      autosave();
      return;
    }
    ++visible_index;
  }
}

void Session::handle_keyboard_nav() {
#ifdef TRACY_ENABLE
  ZoneScopedN("Session::handle_keyboard_nav");
#endif
  if (!ImGui::GetIO().WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
      manager.load_previous();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
      manager.load_next();
    }
  }

  sync_search_selection_bounds();
  sync_billed_selection_bounds();

  if (keyboard_nav_mode == KeyboardNavMode::Search) {
    handle_search_keyboard_nav();
  } else {
    handle_billed_keyboard_nav();
  }
}

void Session::render_same_as_popup() {
  const auto *image_path = current_image_path();
  if (ImGui::BeginPopup("Same As Bill")) {
    ImGui::TextUnformatted("Copy billed entries from another image");
    ImGui::Separator();

    constexpr int columns = 5;
    constexpr float cell_width = 150.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float popup_width = columns * cell_width + spacing * (columns - 1);
    ImGui::SetNextWindowSize(
        ImVec2(popup_width + ImGui::GetStyle().WindowPadding.x * 2, 480.0f),
        ImGuiCond_Appearing);

    bool found_source = false;
    int column = 0;
    for (const auto &image_name : manager.get_thumbnail_order()) {
      if (image_path != nullptr && image_name == image_path->string()) {
        continue;
      }

      const Thumbnail *thumb = manager.get_thumbnail(image_name);
      if (thumb == nullptr || thumb->texture == nullptr || thumb->width <= 0) {
        continue;
      }

      found_source = true;
      if (column % columns != 0) {
        ImGui::SameLine(0.0f, spacing);
      }

      ImGui::PushID(image_name.c_str());
      ImGui::BeginGroup();

      const int frame_number = manager.get_image_index(image_name) + 1;
      if (frame_number > 0) {
        ImGui::Text("Frame %d", frame_number);
      }

      const float aspect = (float)thumb->height / (float)thumb->width;
      if (ImGui::ImageButton("##thumb", thumb->texture,
                             ImVec2(cell_width, cell_width * aspect))) {
        append_bill_from_image(image_name);
        draw_same_as_popup = false;
        ImGui::CloseCurrentPopup();
      }

      ImGui::TextUnformatted(
          std::filesystem::path(image_name).filename().string().c_str());
      ImGui::EndGroup();
      ImGui::PopID();
      ++column;
    }

    if (!found_source) {
      ImGui::TextDisabled("No other images are available.");
    }

    ImGui::Separator();
    if (ImGui::Button("Close")) {
      draw_same_as_popup = false;
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  } else if (draw_same_as_popup && !ImGui::IsPopupOpen("Same As Bill")) {
    draw_same_as_popup = false;
  }
}

void Session::render_search_results_table() {
  if (!ImGui::BeginTable("##search_results", 4,
                         ImGuiTableFlags_RowBg |
                             ImGuiTableFlags_SizingFixedFit)) {
    return;
  }

  ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 110.0f);
  ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
  ImGui::TableSetupColumn("Bhawan", ImGuiTableColumnFlags_WidthFixed, 50.0f);
  ImGui::TableSetupColumn("Room", ImGuiTableColumnFlags_WidthFixed, 40.0f);
  ImGui::TableHeadersRow();

  for (size_t row = 0; row < search_results.size(); ++row) {
    const auto &line = search_results[row];
    ImGui::TableNextRow();
    if ((int)row == selected_search_index &&
        keyboard_nav_mode == KeyboardNavMode::Search) {
      ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                             ImGui::GetColorU32(ImGuiCol_Header));
      ImGui::SetScrollHereY();
    }

    for (int column = 0; column < 4; ++column) {
      ImGui::TableNextColumn();
      if (column == 0) {
        if (ImGui::Button(line[column].c_str())) {
          increment_for_id(line[column], line[column + 1]);
        }
      } else {
        ImGui::TextUnformatted(line[column].c_str());
      }
    }
  }

  ImGui::EndTable();
}

void Session::render_searcher() {
#ifdef TRACY_ENABLE
  ZoneScopedN("Session::render_searcher");
#endif
  ImGui::BeginChild("Search Window", {0.0f, 650.0f}, ImGuiChildFlags_ResizeY);

  if (focus_search_on_next_frame) {
    ImGui::SetKeyboardFocusHere();
    focus_search_on_next_frame = false;
  }

  if (ImGui::InputTextWithHint("##search_query", "Search", &search_query)) {
    evaluate();
  }
  sync_search_selection_bounds();

  ImGui::SameLine();
  if (ImGui::ArrowButton("Previous", ImGuiDir_Left)) {
    manager.load_previous();
  }
  ImGui::SameLine(0.0f, 0.0f);
  if (ImGui::ArrowButton("Next", ImGuiDir_Right)) {
    manager.load_next();
  }
  ImGui::SameLine();
  if (ImGui::Button("Same As")) {
    draw_same_as_popup = true;
    ImGui::OpenPopup("Same As Bill");
  }

  render_same_as_popup();
  render_search_results_table();
  ImGui::EndChild();
}

void Session::render_billed_table(std::map<std::string, BillEntry> &entries) {
  if (!ImGui::BeginTable("##billed_results", 3,
                         ImGuiTableFlags_RowBg |
                             ImGuiTableFlags_SizingFixedFit)) {
    return;
  }

  ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 110.0f);
  ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
  ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 100.0f);
  ImGui::TableHeadersRow();

  int visible_index = 0;
  for (auto &[student_id, entry] : entries) {
    if (entry.count < 1) {
      continue;
    }

    ImGui::PushID(student_id.c_str());
    ImGui::TableNextRow();
    if (keyboard_nav_mode == KeyboardNavMode::Billed &&
        visible_index == selected_billed_index) {
      ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                             ImGui::GetColorU32(ImGuiCol_Header));
      ImGui::SetScrollHereY();
    }

    ImGui::TableNextColumn();
    ImGui::TextUnformatted(student_id.c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(entry.name.c_str());
    ImGui::TableNextColumn();

    int count = entry.count;
    ImGui::PushItemWidth(100.0f);
    if (ImGui::InputInt("##count", &count)) {
      entry.count = std::max(count, 0);
      autosave();
    }
    ImGui::PopItemWidth();
    ImGui::PopID();
    ++visible_index;
  }

  ImGui::EndTable();
}

void Session::render_billed() {
#ifdef TRACY_ENABLE
  ZoneScopedN("Session::render_billed");
#endif
  ImGui::BeginChild("Billed Window", {0.0f, 0.0f});

  if (focus_billed_on_next_frame) {
    focus_billed_on_next_frame = false;
  }

  auto *entries = current_bill_entries();
  if (entries == nullptr || visible_billed_entry_count() == 0) {
    ImGui::TextDisabled("No billed entries for the current image.");
    ImGui::EndChild();
    return;
  }

  render_billed_table(*entries);
  ImGui::EndChild();
}

void Session::increment_for_id(const std::string &id, const std::string &name) {
  auto *entries = current_bill_entries();
  if (entries == nullptr) {
    log_session_error(path, "Cannot bill student without a current image.");
    return;
  }

  BillEntry &entry = (*entries)[id];
  entry.name = name;
  entry.count += 1;
  autosave();
}

void Session::autosave() {
#ifdef TRACY_ENABLE
  ZoneScopedN("Session::autosave");
#endif
  const std::filesystem::path filepath = path / "save.json";
  const nlohmann::json serialised = bill;
  std::ofstream file(filepath, std::ios::out | std::ios::trunc);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open file: " + filepath.string());
  }

  file << serialised.dump(4);
  if (!file.good()) {
    throw std::runtime_error("Error writing to file: " + filepath.string());
  }
}

void Session::append_bill_from_image(
    const std::filesystem::path &source_image) {
#ifdef TRACY_ENABLE
  ZoneScopedN("Session::append_bill_from_image");
#endif
  const auto *image_path = current_image_path();
  if (image_path == nullptr) {
    return;
  }

  const auto source_it = bill.find(source_image);
  if (source_it == bill.end()) {
    return;
  }

  auto &current_bill = bill[*image_path];
  for (const auto &[student_id, source_entry] : source_it->second) {
    BillEntry &entry = current_bill[student_id];
    entry.name = source_entry.name;
    entry.count += source_entry.count;
  }
  autosave();
}

void Session::load_existing_bill() {
#ifdef TRACY_ENABLE
  ZoneScopedN("Session::load_existing_bill");
#endif
  const std::filesystem::path filepath = path / "save.json";
  if (!std::filesystem::exists(filepath)) {
    return;
  }

  std::ifstream file(filepath);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open file: " + filepath.string());
  }

  nlohmann::json json_data;
  file >> json_data;
  bill = json_data.get<BillMap>();
}

const char *get_random_civ6_quote(void) {
  static const char *quotes[] = {
      "No man ever wetted clay and then left it, as if there would be bricks "
      "by chance and fortune. \n— Plutarch",
      "I thought clay must feel happy in the good potter’s hand. \n— Janet "
      "Fitch",

      "If there are no dogs in Heaven, then when I die I want to go where they "
      "went. \n— Will Rogers",
      "I am fond of pigs. Dogs look up to us. Cats look down on us. Pigs treat "
      "us as equals. \n— Winston S. Churchill",

      "Who deserves more credit than the wife of a coal miner? \n— Merle "
      "Travis",
      "When you find yourself in a hole, quit digging. \n— Will Rogers",

      "Vessels large may venture more, but little boats should keep near "
      "shore. \n— Benjamin Franklin",
      "It is not that life ashore is distasteful to me. But life at sea is "
      "better. \n— Sir Francis Drake",

      "I don’t believe in astrology; I’m a Sagittarius and we’re skeptical. "
      "\n— "
      "Arthur C. Clarke",
      "A physician without a knowledge of astrology has no right to call "
      "himself a physician. \n— Hippocrates",

      "Thousands have lived without love, not one without water. \n— W. H. "
      "Auden",
      "The man who has grit enough to bring about the afforestation or the "
      "irrigation of a country is not less worthy of honor than its conqueror. "
      "\n— Sir John Thomson",

      "I shot an arrow into the air. It fell to earth, I knew not where. \n— "
      "Henry Wadsworth Longfellow",
      "May the forces of evil become confused while your arrow is on its way "
      "to the target. \n— George Carlin",

      "Writing means sharing. It’s part of the human condition to want to "
      "share things thoughts, ideas, opinions. \n— Paulo Coelho",
      "Writing is easy. All you have to do is cross out the wrong words. \n— "
      "Mark Twain",

      "Each of us is carving a stone, erecting a column, or cutting a piece of "
      "stained glass in the construction of something much bigger than "
      "ourselves. \n— Adrienne Clarkson",
      "When wasteful war shall statues overturn, and broils root out the work "
      "of masonry. \n— William Shakespeare",

      "Bronze is the mirror of the form, wine of the mind. \n— Aeschylus",
      "I’m also interested in creating a lasting legacy… because civ 6 will "
      "last for thousands of years. \n— Richard MacDonald",

      "Sometimes the wheel turns slowly, but it turns. \n— Lorne Michaels",
      "Don’t reinvent the wheel, just realign it. \n— Anthony D’Angelo",

      "And all I ask is a tall ship and a star to steer her by. \n— John "
      "Masefield",
      "Set your course by the stars, not by the lights of every passing ship. "
      "\n— Omar Bradley",

      "Wealth consists not in having great possessions, but in having few "
      "wants. \n— Epictetus",
      "Money, if it does not bring you happiness, will at least help you be "
      "miserable in comfort. \n— Helen Gurley Brown",

      "No hour of life is wasted that is spent in the saddle. \n— Winston "
      "Churchill",
      "A man on a horse is spiritually as well as physically bigger than a man "
      "on foot. \n— John Steinbeck",

      "The Lord made us all out of iron. Then he turns up the heat to forge "
      "some of us into steel. \n— Marie Osmond",
      "Everything has its limit iron ore cannot be educated into gold. \n— "
      "Mark "
      "Twain",

      "I cannot imagine any condition which would cause a ship to founder … "
      "Modern shipbuilding has gone beyond that. \n— Capt. E.J. Smith",
      "There is nothing but a plank between a sailor and eternity. \n— Thomas "
      "Gibbons",

      "Without mathematics, there’s nothing you can do. Everything around you "
      "is mathematics. Everything around you is numbers. \n— Shakuntala Devi",
      "If I were again beginning my studies, I would follow the advice of "
      "Plato and start with mathematics. \n— Galileo Galilei",

      "Create with the heart; build with the mind. \n— Criss Jami",
      "The four building blocks of the universe are fire, water, gravel and "
      "vinyl. \n— Dave Barry",

      "One man’s ‘magic’ is another man’s engineering. \n— Robert Heinlein",
      "Normal people believe that if it ain’t broke, don’t fix it. Engineers "
      "believe that if it ain’t broke, it doesn’t have enough features yet. "
      "\n— "
      "Scott Adams",

      "Tactics mean doing what you can with what you have. \n— Saul Alinsky",
      "Strategy requires thought; tactics require observation. \n— Max Euwe",

      "We are all apprentices in a craft where no one ever becomes a master. "
      "\n— "
      "Ernest Hemingway",
      "There is no easy way to train an apprentice. My two tools are example "
      "and nagging. \n— Lemony Snicket",

      "Few inventions have been so simple as the stirrup, but few have had so "
      "catalytic an influence on history. \n— Lynn White Jr.",
      "Betwixt the stirrup and the ground, Mercy I asked, mercy I found. \n— "
      "William Camden",

      "I’d imagine the whole world as one big machine. Machines never come "
      "with any spare parts, you know. They always come with the exact amount "
      "they need. \n— Hugo Cabret",
      "Remember that people break down, too, not just machinery. \n— Gregory "
      "Benford",

      "The purpose of education is to replace an empty mind with an open one. "
      "\n— Malcolm Forbes",
      "It is the mark of an educated mind to be able to entertain a thought "
      "without accepting it. \n— Aristotle",

      "Blast Build Battle \n— Motto of the U.S. 6th Engineer Brigade",
      "The more science intervenes in warfare, the more will be the need for "
      "engineers in the field armies. \n— Bernard Montgomery",

      "Rocks in my path? I keep them all. With them I shall build my castle. "
      "\n— "
      "Nemo Nox",
      "If you see a castle under fog, you must walk there to meet the "
      "extraordinary dreams. \n— Mehmet Murat Ildan",

      "If your actions inspire others to dream more, learn more, do more and "
      "become more, you are a cartographer. \n— John Quincy Adams",
      "Not all who wander are lost. \n— J.R.R. Tolkien",

      "People can have the Model T in any color so long as it’s black. \n— "
      "Henry "
      "Ford",
      "What can be labeled, packaged, mass produced is neither truth nor art. "
      "\n— Marty Rubin",

      "If you owe the bank $100 that’s your problem. If you owe the bank $100 "
      "million, that’s the bank’s problem. \n— J. Paul Getty",
      "I saw a bank that said 24Hour Banking, but I didn’t have that much "
      "time. \n— Steven Wright",

      "The real use of gunpowder is to make all men tall. \n— Thomas Carlyle",
      "Man is a military animal, glories in gunpowder, and loves parades. \n— "
      "Philip Bailey",

      "The pen might not be mightier than the sword, but maybe the printing "
      "press is heavier than the siege weapon. \n— Terry Pratchett",
      "What gunpowder did for war the printing press has done for the mind. "
      "\n— "
      "Wendell Phillips",

      "There is little man has made that approaches anything in nature, but a "
      "sailing ship does. \n— Allan Villiers",
      "It’s not the towering sails, but the unseen wind that moves a ship. \n— "
      "English Proverb",

      "Astronomy compels the soul to look upwards and leads us from this world "
      "to another. \n— Plato",
      "Astronomy’s much more fun when you’re not an astronomer. \n— Brian May",

      "The lowest is to attack a city. Siege of a city is only done as a last "
      "resort. \n— Sun Tzu",
      "All the best romances bloom in the midst of a good siege. \n— Miles "
      "Cameron",

      "Claims that cannot be tested are worthless. \n— Carl Sagan",
      "If facts don’t fit the theory, change the facts. \n— Albert Einstein",

      "However beautiful the strategy, you should occasionally look at the "
      "results. \n— Winston Churchill",
      "No one starts a war without knowing what they intend to achieve. \n— "
      "Karl "
      "von Clausewitz",

      "To err is human, but to really foul things up you need a computer. \n— "
      "Paul R. Ehrlich",
      "The good thing about computers is that they do what you tell them to "
      "do. \n— Ted Nelson",

      "If you go on with this nuclear arms race, all you are going to do is "
      "make the rubble bounce. \n— Winston Churchill",
      "Leave the atom alone. \n— E. Y. Harburg",

      "Mr. Watson… Come here… I want to see you. \n— Alexander Graham Bell",
      "The single biggest problem in communication is the illusion that it has "
      "taken place. \n— George Bernard Shaw",

      "When God said, Let there be light, he surely must have meant perfectly "
      "coherent light. \n— Charles Townes",
      "I’m a big laser believer I really think they are the wave of the "
      "future. \n— Courteney Cox",

      "I’ll be back. \n— Terminator",

      "If technology is the engine of change, then nanotechnology is the fuel "
      "for humanity’s future. \n— Natasha Vita-More",
      "Many rules had begun to bend at the hand of nanotechnology. \n— Matt "
      "Spire",

      "There is nothing like a dream to create the future. \n— Victor Hugo",
      "Even though the future seems far away, it is actually beginning right "
      "now. \n— Mattie Stepanek"};

  int num_quotes = sizeof(quotes) / sizeof(quotes[0]);
  int index = rand() % num_quotes;
  return quotes[index];
}

void Session::open_export_modal() {
  if (!exporting) {
    prepare_export_queue();
  }
  draw_exporting = true;
  quote = get_random_civ6_quote();
}

void Session::prepare_export_queue() {
#ifdef TRACY_ENABLE
  ZoneScopedN("Session::prepare_export_queue");
#endif
  pending.clear();
  export_font_data.clear();
  if (export_output_directory.empty()) {
    export_output_directory =
        (std::filesystem::path("./Data") / path.filename()).string();
  }

  size_t total_items = 0;
  for (const auto &[image_path, entries] : bill) {
    for (const auto &[student_id, entry] : entries) {
      (void)student_id;
      total_items += std::max(entry.count, 0);
    }
  }
  pending.reserve(total_items);

  const std::string roll = path.filename().string();
  for (const auto &[image_path, entries] : bill) {
    for (const auto &[student_id, entry] : entries) {
      if (entry.count < 1) {
        continue;
      }

      const ExportInfo info =
          database->get_export_information_from_id(student_id);
      const std::string watermark = info.bhawan + " " + info.roomno;
      for (int copy_index = 1; copy_index <= entry.count; ++copy_index) {
        const std::string filename = roll + "_" + image_path.stem().string() +
                                     "_" + info.bhawan + "_" + info.roomno +
                                     "_" + student_id +
                                     std::to_string(copy_index) + "_" + ".jpg";
        pending.push_back(
            {image_path,
             std::filesystem::path(export_output_directory) / filename,
             watermark, image_path.filename().string()});
      }
    }
  }

  export_total = static_cast<int>(pending.size());
  export_progress = 0;
  export_completed = false;
  export_active_items.clear();

  std::ostringstream status;
  status << "Ready: " << pending.size() << " image";
  if (pending.size() != 1) {
    status << 's';
  }
  export_status_message = status.str();
}

void Session::start_export() {
#ifdef TRACY_ENABLE
  ZoneScopedN("Session::start_export");
#endif
  if (exporting || pending.empty()) {
    return;
  }
  if (export_worker.joinable()) {
    export_worker.join();
  }

  export_font_data.clear();
  if (export_apply_watermark) {
    std::ifstream font_file("./Data/Quantico-Regular.ttf", std::ios::binary);
    export_font_data.assign(std::istreambuf_iterator<char>(font_file), {});
    if (export_font_data.empty()) {
      export_status_message = "Font file missing: ./Data/Quantico-Regular.ttf";
      log_session_error(path, export_status_message);
      return;
    }
  }

  std::filesystem::create_directories(export_output_directory);
  export_progress = 0;
  export_completed = false;
  exporting = true;

  {
    std::lock_guard<std::mutex> lock(export_status_mutex);
    export_active_items.assign(1, "Waiting to start");
  }

  std::ostringstream status;
  status << "Writing to " << export_output_directory;
  export_status_message = status.str();
  export_worker = std::thread([this]() { export_images(); });
}

void Session::finish_export_if_ready() {
  if (!exporting || export_progress.load() < export_total) {
    return;
  }

  exporting = false;
  export_completed = true;
  if (export_worker.joinable()) {
    export_worker.join();
  }

  std::lock_guard<std::mutex> lock(export_status_mutex);
  export_active_items.clear();

  std::ostringstream status;
  status << "Finished: " << export_total << " image";
  if (export_total != 1) {
    status << 's';
  }
  status << " written to " << export_output_directory;
  export_status_message = status.str();
}

void Session::render_export_modal() {
#ifdef TRACY_ENABLE
  ZoneScopedN("Session::draw_export_modal");
#endif
  finish_export_if_ready();

  const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
  ImGui::OpenPopup("Export Roll");

  if (!ImGui::BeginPopupModal("Export Roll", nullptr,
                              ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }

  ImGui::PushTextWrapPos(0.0f); // wrap at window edge
  ImGui::TextColored(ImVec4(0.93f, 0.73f, 0.24f, 1.0f), quote.c_str());
  ImGui::PopTextWrapPos();

  ImGui::Separator();

  ImGui::Text("Roll");
  ImGui::SameLine(150.0f);
  ImGui::TextUnformatted(path.filename().string().c_str());

  ImGui::Text("Queued images");
  ImGui::SameLine(150.0f);
  ImGui::Text("%d", export_total);

  ImGui::Text("Destination");
  ImGui::SameLine(150.0f);
  ImGui::SetNextItemWidth(340.0f);
  if (ImGui::InputText("##export_destination", &export_output_directory,
                       ImGuiInputTextFlags_AutoSelectAll) &&
      !exporting) {
    prepare_export_queue();
  }

  ImGui::Text("Options");
  ImGui::SameLine(150.0f);
  if (ImGui::Checkbox("Stamp watermark", &export_apply_watermark) &&
      !exporting) {
    export_status_message = export_apply_watermark
                                ? "Watermark stamping enabled"
                                : "Watermark stamping disabled";
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset destination") && !exporting) {
    export_output_directory.clear();
    prepare_export_queue();
  }

  ImGui::Spacing();
  ImGui::TextUnformatted(export_status_message.c_str());

  const float progress = export_total > 0
                             ? static_cast<float>(export_progress.load()) /
                                   static_cast<float>(export_total)
                             : 0.0f;
  const std::string progress_text = std::to_string(export_progress.load()) +
                                    " / " + std::to_string(export_total);
  ImGui::ProgressBar(exporting ? progress : (export_completed ? 1.0f : 0.0f),
                     ImVec2(520, 0), progress_text.c_str());

  std::vector<std::string> active_items;
  {
    std::lock_guard<std::mutex> lock(export_status_mutex);
    active_items = export_active_items;
  }

  ImGui::Spacing();
  ImGui::Text("Current image");
  ImGui::BeginChild("ExportActiveWork", ImVec2(520, 120), true);
  if (active_items.empty()) {
    ImGui::TextDisabled(exporting ? "Starting export..."
                                  : "Nothing is running right now.");
  } else {
    for (const auto &item : active_items) {
      ImGui::BulletText("%s", item.c_str());
    }
  }
  ImGui::EndChild();

  ImGui::Spacing();
  const bool can_start =
      !exporting && export_total > 0 && !export_output_directory.empty();
  if (!can_start) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button(export_completed ? "Run Again" : "Start Export",
                    ImVec2(150, 0))) {
    start_export();
  }
  if (!can_start) {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  if (ImGui::Button(exporting ? "Close" : "Done", ImVec2(120, 0))) {
    draw_exporting = false;
    if (!exporting) {
      ImGui::CloseCurrentPopup();
    }
  }

  ImGui::EndPopup();
}

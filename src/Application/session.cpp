#include "application.h"
#include "imgui.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
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
                 SDL_GPUDevice *device, std::shared_ptr<ImageEditor> editor)
    : manager(device, path, editor), path(std::move(path)), database(database) {
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
      "now. \n— Mattie Stepanek",

      "Yet in this captious and intenible sieve \nI still pour in the waters "
      "of "
      "my love \nAnd lack not to lose still: thus, Indian-like, \nReligious in "
      "mine error, I adore \nThe sun, that looks upon his worshipper, \nBut "
      "knows of him no more. \n—William Shakespeare",

      "The ascent to the highest story is by stairs, and at their side are "
      "water "
      "engines, by means of which persons, appointed expressly for the "
      "purpose, "
      "are continually employed in raising water from the Euphrates into the "
      "garden. \n—Strabo",

      "Can you imagine trying to talk six hundred people into helping you drag "
      "a "
      "fifty-ton stone eighteen miles across the countryside and muscle it "
      "into "
      "an upright position, and then saying, 'Right, lads! Another twenty like "
      "that … and then we can party!' \n—Bill Bryson",

      "When I saw the house of Artemis that mounted to the clouds, those other "
      "marvels lost their brilliancy, and I said, 'Lo, apart from Olympus, the "
      "Sun never looked on aught so grand.' \n—Antipater of Sidon",

      "Come, let us build ourselves a city, with a tower that reaches to the "
      "heavens, so that we may make a name for ourselves. \n—Genesis 11:4",
      "From the heights of these pyramids, forty centuries look down on us. "
      "\n—Napoleon Bonaparte",

      "This Lighthouse was the cynosure of all eyes. \n—Henry David Thoreau",
      "I sprang upon the swift ship in the form of a dolphin, pray to me as "
      "Apollo Delphinius; also the altar itself shall be called Delphinius and "
      "overlooked forever. \n—Homer",

      "My ancestor Darius made this Apadana, but it was burnt down. By the "
      "grace "
      "of Ahuramazda, Anahita, and Mithra, I reconstructed this Apadana. "
      "\n—Artaxerxes II",
      "While the Colosseum stands, Rome shall stand; when the Colosseum falls, "
      "Rome shall fall; when Rome falls, the world shall fall. \n—Saint Bede",
      "At Rhodes was set up a Colossus of seventy cubits high, representing "
      "the "
      "Sun … the artist expended as much bronze on it as seemed likely to "
      "create "
      "a dearth in the mines. \n—Philo of Byzantium",
      "We can roam the bloated stacks of the Library of Alexandria, where all "
      "imagination and knowledge are assembled; we can recognize in its "
      "destruction the warning that all we gather will be lost. \n—Alberto "
      "Manguel",
      "At the south-west corner a large perpendicular mass of sandstone has "
      "become separated by a deep fissure from the body of the mountain … it "
      "has "
      "all the appearance of a colossal statue. \n—E. A. Wallis Budge",
      "Eagles fly, The turrets round, and seem to flout the sky; They eye "
      "those "
      "walls now crumbling in decay, And scare from quiet rest their destined "
      "prey. \n—Samuel Prout Hill",
      "In a dusty, bustling corner of the Indian state of Bihar, there is a "
      "magical place that one might think of as the hub of Buddhism. "
      "\n—Visitor's "
      "Guide, Bodh Gaya",
      "A vast tomb lies over me in Halicarnassus, of such dimensions, of such "
      "exquisite beauty as no other shade can boast. \n—Lucian of Samosata",
      "Petra is a brilliant display of man's artistry in turning barren rock "
      "into majestic wonder. \n—Edward Dawson",
      "Upon the head of the god was an olive crown; in his right hand he bore "
      "a "
      "winged figure of Victory. \n—Samuel Augustus Mitchell",
      "There were seven wonders in the world, and the discovery of the "
      "Terracotta Army, we may say, is the eighth miracle of the world. "
      "\n—Jacques "
      "Chirac",
      "Everything here appears calculated to inspire kind and happy feelings, "
      "for everything is delicate and beautiful. \n—Washington Irving",
      "The temple is surrounded by a moat, and access is by a single bridge, "
      "protected by two stone tigers so grand and fearsome as to strike terror "
      "into the visitor. \n—Diogo do Couto",
      "The Great Ball Court is also very impressive. I would like to have seen "
      "them play a game, although it sounds like the end was pretty violent. I "
      "think it was safer to be a spectator. \n—IslaDeb",
      "Church and State, Soul and Body, God and Man, are all one at Mont Saint "
      "Michel, and the business of all is to fight, each in his own way, or to "
      "stand guard for each other. \n—Henry Adams",
      "Scholars are the heirs of the prophets, for the prophets did not leave "
      "behind a legacy of wealth but that of knowledge. \n—Musnad al-Bazzar "
      "10/68",
      "All other lands found on the western side of the boundary shall belong "
      "to the King and Queen of Castille\n—and their successors. —Treaty of "
      "Tordesillas",
      "The whole palace complex is built along a central axis, the axis of the "
      "world, everything in the four directions suspend from this central "
      "point represented by these palaces. \n—Jeffrey Riegel",
      "King Solomon gave the Queen of Sheba all she desired and asked for; "
      "besides what he had given her out of his royal bounty. So she turned "
      "and went to her own country, she and her servants. \n—1 Kings, 10:13",
      "With self-government is freedom, and with freedom is justice and "
      "patriotism. \n—Lajos Kossuth",
      "Therefore will not we fear, though the earth be removed, and though the "
      "mountains be carried into the midst of the sea; Though the waters "
      "thereof roar and be troubled, though the mountains shake with the "
      "swelling thereof. \n—Psalms 46:2-3",
      "One had a twisted design, red, on a green ground; another, all prickly "
      "angles, yellow and black; a third was ornamented with scales of blue "
      "and crimson; a [fourth] was in quarters like a melon. \n—Katharine "
      "Blanche Guthrie",
      "Did you ever build a castle in the air? Here is one, brought down to "
      "earth and fixed for the wonder of ages. \n—Bayard Taylor",
      "No vessel could pass these guns without being seriously assailed. "
      "\n—Laure Junot Abrantes",
      "The Commonwealth of Venice in their armory have this inscription: "
      "'Happy is that city which in time of peace thinks of war.' \n—Robert "
      "Burton",
      "The first time I stepped onto the rooftop of the Potala Palace, I felt, "
      "as never before or since, as if I were stepping onto the rooftop of my "
      "being: onto some dimension of consciousness that I'd never visited "
      "before. \n—Pico Iyer",
      "Bolshoi Ballet is a universe of the imagination, a place of magic and "
      "enchantment, beauty and romance. Its many worlds vibrate with graceful "
      "dancers, glorious music, and sumptuous costumes. \n—Trudy Garfunkel",
      "The clever men at Oxford... Know all that there is to be knowed. But "
      "they none of them know one half as much... As intelligent Mr. Toad! "
      "\n—Kenneth Grahame",
      "The industrial heart of Germany practically stopped beating. Hardly "
      "anyone worked; hardly anything ran. The population of the Ruhr area … "
      "had to be supported by the rest of the country. \n—Adam Fergusson",
      "Here at our sea-washed, sunset gates shall stand a mighty woman with a "
      "torch, whose flame is the imprisoned lightning. \n—Emma Lazarus",
      "Don't watch the big clock; do what it does. Keep going. \n—Sam Levenson",
      "Museums are on the front lines of the fight for culture, of good with "
      "evil - in any case, of the fight against platitudes and primitiveness. "
      "\n—Mikhail Piotrovsky, Director of the State Hermitage",
      "For scientific leadership, give me Scott; for swift and efficient "
      "travel, Amundsen. \n—Sir Raymond Priestley",
      "There's no business like show business. \n—Irving Berlin, Annie Get "
      "Your "
      "Gun",
      "Thus, the sculpture serves to give people one thought - 'Everything is "
      "in God's hands.' \n—Sergey Semenov",
      "I ought to be jealous of the tower. She is more famous than I am. "
      "\n—Gustave Eiffel",
      "The Golden Gate of Sleep unbar / Where Strength and Beauty, met "
      "together, / Kindle their image like a star / In a sea of glassy "
      "weather! \n—Percy Bysshe Shelley",
      "Heaven is under our feet as well as over our heads. \n—Henry David "
      "Thoreau",
      "Down through its history, only three people have managed to silence the "
      "Maracana: the Pope, Frank Sinatra, and me. \n—Alcides Ghiggia, "
      "Uruguayan "
      "soccer player",
      "An opera begins long before the curtain goes up and ends long after it "
      "has come down. It starts in my imagination, it becomes my life, and it "
      "stays part of my life long after I've left the opera house. \n—Maria "
      "Callas",

      // Code of Laws
      "It is not wisdom but authority that makes a law. \n—Thomas Hobbes",
      "At his best, man is the noblest of all animals; separated from law and "
      "justice he is the worst. \n—Aristotle",
      // Craftsmanship
      "Without craftsmanship, inspiration is a mere reed shaken in the wind. "
      "\n—Johannes Brahms",
      "Skill without imagination is craftsmanship and gives us many useful "
      "objects such as wickerwork picnic baskets. \n—Tom Stoppard",
      // Foreign Trade
      "Every nation lives by exchanging. \n—Adam Smith",
      "That's the positive aspect of trade I suppose. The world gets stirred "
      "up together. \n—Isabel Hoving",
      // Military Tradition
      "Bravery is being the only one who knows you're afraid. \n—Colonel David "
      "Hackworth",
      "I don't underrate the value of military knowledge, but if men make war "
      "in slavish obedience to rules, they will fail. \n—Ulysses S. Grant",
      // State Workforce
      "A strong economy begins with a strong, well-educated workforce. \n—Bill "
      "Owens",
      "It is equally important to have a happy and engaged workforce as it is "
      "to have a profitable bottom line. \n—Vern Dosch",
      // Early Empire
      "Look back over the past, with its changing empires that rose and fell, "
      "and you can foresee the future, too. \n—Marcus Aurelius",
      "It was luxuries like air conditioning that brought down the Roman "
      "Empire. With air conditioning their windows were shut; they couldn't "
      "hear the barbarians coming. \n—Garrison Keillor",
      // Mysticism
      "Mysticism is the mistake of an accidental and individual symbol for a "
      "universal one. \n—Ralph Waldo Emerson",
      "I like to say I practice militant mysticism. I'm absolutely sure of "
      "some things that I don't quite know. \n—Rob Bell",
      // Games and Recreation
      "If bread is the first necessity of life, recreation is a close second. "
      "\n—Edward Bellamy",
      "People who cannot find time for recreation are sooner or later to find "
      "time for illness. \n—John Wanamaker",
      // Political Philosophy
      "Politics is the art of the possible, the attainable the art of the next "
      "best. \n—Otto von Bismarck",
      "Divide and rule, a sound motto. Unite and lead, a better one. \n—Johann "
      "Wolfgang von Goethe",
      // Drama and Poetry
      "The poets have been mysteriously silent on the subject of cheese. \n—G. "
      "K. Chesterton",
      "All the world's a stage, and all the men and women merely players. "
      "\n—William Shakespeare",
      // Military Training
      "If it's natural to kill, how come men have to go into training to learn "
      "how? \n—Joan Baez",
      "Those who in quarrels interpose, must often wipe a bloody nose. \n—John "
      "Gay",
      // Defensive Tactics
      "Invincibility lies in the defense; the possibility of victory in the "
      "attack. \n—Sun Tzu",
      "Defense is superior to opulence. \n—Adam Smith",
      // Recorded History
      "I've lived through some terrible things in my life, some of which "
      "actually happened. \n—Mark Twain",
      "History is the version of past events that people have decided to agree "
      "upon. \n—Napoleon Bonaparte",
      // Theology
      "We can no more have exact religious thinking without theology, than "
      "exact mensuration and astronomy without mathematics, or exact "
      "ironmaking without chemistry. \n—John Hall",
      "Man suffers only because he takes seriously what the gods made for fun. "
      "\n—Alan W. Watts",
      // Naval Tradition
      "A good navy is not a provocation to war. It is the surest guaranty of "
      "peace. \n—Theodore Roosevelt",
      "The Navy has both a tradition and a future and we look with pride and "
      "confidence in both directions. \n—Arleigh Burke",
      // Feudalism
      "In democracy it's your vote that counts; in feudalism it's your count "
      "that votes. \n—Mogens Jallberg",
      "With the advance of feudalism came the growth of iron armor, until, at "
      "last, a fighting-man resembled an armadillo. \n—John Boyle O'Reilly",
      // Civil Service
      "It's all papers and forms, the entire civil service is like a fortress "
      "made of papers, forms and red tape. \n—Alexander Ostrovsky",
      "The taxpayer that's someone who works for the federal government but "
      "doesn't have to take the civil service examination. \n—Ronald Reagan",
      // Mercenaries
      "In peace one is despoiled by mercenaries; in war by one's enemies. "
      "\n—Niccolo Machiavelli",
      "Being a mercenary, though … Hey, we just go wherever there's a mixture "
      "of money and trouble. \n—Howard Tayler",
      // Medieval Faires
      "All that glisters is not gold; often have you heard that told. "
      "\n—William "
      "Shakespeare",
      "There are very honest people who do not think that they have had a "
      "bargain unless they have cheated a merchant. \n—Anatole France",
      // Guilds
      "Every man should make his son learn some useful trade or profession, so "
      "that in these days of changing fortunes … they may have something "
      "tangible to fall back upon. \n—Phineas T. Barnum",
      "You can't go around arresting the Thieves' Guild. I mean, we'd be at it "
      "all day! \n—Terry Pratchett",
      // Divine Right
      "I conclude then this point touching upon the power of kings with this "
      "axiom of divinity, That as to dispute what God may do is blasphemy … so "
      "it is sedition to dispute what a king may do. \n—King James I",
      "Listen, strange women lying in ponds distributing swords is no basis "
      "for a system of government … You can't expect to wield supreme power "
      "just 'cause some watery tart threw a sword at you! \n—Monty Python",
      // Exploration
      "The day we stop exploring is the day we commit ourselves to live in a "
      "stagnant world, devoid of curiosity, empty of dreams. \n—Neil deGrasse "
      "Tyson",
      "We shall not cease from exploration, and the end of all our exploring "
      "will be to arrive where we started and know the place for the first "
      "time. \n—T.S. Eliot",
      // Humanism
      "The four characteristics of humanism are curiosity, a free mind, belief "
      "in good taste, and belief in the human race. \n—E.M. Forster",
      "You must not lose faith in humanity. Humanity is like an ocean; if a "
      "few drops of the ocean are dirty, the ocean does not become dirty. "
      "\n—Mahatma Gandhi",
      // Diplomatic Service
      "In diplomacy there are two kinds of problems: small ones and large "
      "ones. The small ones will go away by themselves, and the large ones you "
      "will not be able to do anything about. \n—Patrick McGuinness",
      "A diplomat is a man who always remembers a woman's birthday but never "
      "remembers her age. \n—Robert Frost",
      // Reformed Church
      "I don't like to commit myself about Heaven and Hell, you see, I have "
      "friends in both places. \n—Mark Twain",
      "The three great elements of modern civilization: gun powder, printing, "
      "and the Protestant religion. \n—Thomas Carlyle",
      // Mercantilism
      "In a market economy, however, the individual has some possibility of "
      "escaping from the power of the state. \n—Peter Berger",
      "Having seen a nonmarket economy, I suddenly understood much better what "
      "I liked about a market economy. \n—Esther Dyson",
      // The Enlightenment
      "New opinions are always suspected, and usually opposed, without any "
      "other reason but because they are not already common. \n—John Locke",
      "Whatever is contrary to nature is contrary to reason, and whatsoever is "
      "contrary to reason is absurd. \n—Baruch Spinoza",
      // Colonialism
      "Remember that politics, colonialism, imperialism and war also "
      "originated in the human brain. \n—Vilayanur Ramachandran",
      "Colonialism. The enforced spread of the rule of reason. But who is "
      "going to spread it among the colonizers? \n—Anthony Burgess",
      // Civil Engineering
      "Improvement makes straight roads; but the crooked roads without "
      "improvement are roads of Genius. \n—William Blake",
      "A common mistake that people make when trying to design something "
      "completely foolproof is to underestimate the ingenuity of complete "
      "fools. \n—Douglas Adams",
      // Nationalism
      "It is nationalism which engenders nations, and not the other way round. "
      "\n—Ernest Gellner",
      "Human nature, as manifested in tribalism and nationalism, provides the "
      "momentum of the machinery of human evolution. \n—Arthur Keith",
      // Opera and Ballet
      "Opera is when a guy gets stabbed in the back and, instead of bleeding, "
      "he sings. \n—Robert Benchley",
      "It [ballet] projects a fragile kind of strength and a certain "
      "inflexible precision. \n—Ayn Rand",
      // Natural History
      "Had I been present at the Creation, I would have given some useful "
      "hints for the better ordering of the universe. \n—Nelson Algren",
      "In all works on Natural History, we constantly find details of the "
      "marvelous adaptation of animals to their food, their habits, and the "
      "localities in which they are found. \n—Alfred Wallace",
      // Scorched Earth
      "War is hell. \n—William Tecumseh Sherman",
      "I only understand friendship or scorched earth. \n—Roger Ailes",
      // Urbanization
      "It's the Industrial Revolution and the growth of urban concentrations "
      "that led to a sense of anonymity. \n—Vint Cerf",
      "What I like about cities is that everything is king size, the beauty "
      "and the ugliness. \n—Joseph Brodsky",
      // Capitalism
      "The inherent vice of capitalism is the unequal sharing of blessings; "
      "the inherent virtue of socialism is the equal sharing of miseries. "
      "\n—Winston Churchill",
      "Always try to rub up against money, for if you rub against money long "
      "enough, some of it may rub off on you. \n—Damon Runyon",
      // Conservation
      "Water and air, the two essentials on which life depends, have become "
      "global garbage cans. \n—Jacques Yves Cousteau",
      "Destroying rainforest for economic gain is like burning a Renaissance "
      "painting to cook a meal. \n—Edward Wilson",
      // Mass Media
      "The effect of the mass media is not to elicit belief but to maintain "
      "the apparatus of addiction. \n—Christopher Lasch",
      "If you don't read the newspaper, you're uninformed. If you read the "
      "newspaper, you're misinformed. \n—Mark Twain",
      // Mobilization
      "When they are preparing for war, those who rule by force speak most "
      "copiously about peace until they have completed the mobilization "
      "process. \n—Stefan Zweig",
      "In order to rally people, governments need enemies … if they do not "
      "have a real enemy, they will invent one in order to mobilize us. "
      "\n—Nhat "
      "Hanh",
      // Nuclear Program
      "The release of atom power has changed everything except our way of "
      "thinking…the solution to this problem lies in the heart of mankind. If "
      "only I had known, I should have become a watchmaker. \n—Albert Einstein",
      "The children of the nuclear age, I think, were weakened in their "
      "capacity to love. Hard to love, when you're bracing yourself for "
      "impact. \n—Martin Amis",
      // Ideology
      "It has been demonstrated that no system, not even the most inhuman, can "
      "continue to exist without an ideology. \n—Joe Slovo",
      "Slowly, ideas lead to ideology, lead to policies that lead to actions. "
      "\n—Nandan Nilekani",
      // Suffrage
      "Why is a woman to be treated differently? Woman suffrage will succeed, "
      "despite this miserable guerilla opposition. \n—Victoria Woodhull",
      "Men, their rights, and nothing more; women, their rights, and nothing "
      "less. \n—Susan B. Anthony",
      // Totalitarianism
      "Only the mob and the elite can be attracted by the momentum of "
      "totalitarianism itself. The masses have to be won by propaganda. "
      "\n—Hannah Arendt",
      "The ultimate end of any ideology is totalitarianism. \n—Tom Robbins",
      // Class Struggle
      "There's class warfare, all right, but it's my class, the rich class, "
      "that's making war … and we're winning. \n—Warren Buffett",
      "The class struggle necessarily leads to the dictatorship of the "
      "proletariat. \n—Karl Marx",
      // Cold War
      "From Stettin in the Baltic to Trieste in the Adriatic, an iron curtain "
      "has descended across the continent. \n—Winston Churchill",
      "The Cold War is not thawing; it is burning with a deadly heat. "
      "\n—Richard "
      "Nixon",
      // Professional Sports
      "If winning isn't everything, why do they keep score? \n—Vince Lombardi",
      "Sports do not build character. They reveal it. \n—Heywood Broun",
      // Cultural Heritage
      "A people without the knowledge of their past history, origin and "
      "culture is like a tree without roots. \n—Marcus Garvey",
      "You don't stumble upon your heritage. It's there, just waiting to be "
      "explored and shared. \n—Robbie Robertson",
      // Rapid Deployment
      "A good plan violently executed right now is far better than a perfect "
      "plan executed next week. \n—George S. Patton",
      "Never mind the maneuvers, just go straight at them. \n—Horatio Nelson",
      // Space Race
      "We choose to go to the moon in this decade and do the other things, not "
      "because they are easy, but because they are hard. \n—John F. Kennedy",
      "NASA spent millions of dollars inventing the ballpoint pen so they "
      "could write in space. The Russians took a pencil. \n—Will Chabot",
      // Environmentalism
      "I shall return again to the light of the sun, to prepare a home for thy "
      "descendants. \n—Argonautica, Apollonius Rhodius",
      // Globalization
      "It has been said that arguing against globalization is like arguing "
      "against the laws of gravity. \n—Kofi Annan",
      "One day there will be no borders, no boundaries, no flags and no "
      "countries and the only passport will be the heart. \n—Carlos Santana",
      // Social Media
      "Which of all my important nothings shall I tell you first? \n—Jane "
      "Austen",
      "Distracted from distraction by distraction! \n—T.S. Eliot",
      // Near Future Governance
      "Justice is the bond of men in states, for the administration of "
      "justice, which is the determination of what is just, is the principle "
      "of order in political society. \n—Aristotle",
      // Venture Politics
      "Over himself, over his own body and mind, the individual is sovereign. "
      "\n—John Stuart Mill",
      // Distributed Sovereignty
      "Multitudes, multitudes in the valley of decision: for the day of the "
      "Lord is near in the valley of decision. \n—Joel 3:14",
      // Optimization Imperative
      "We find every where men of mechanical genius, of great general "
      "acuteness, and discriminative understanding, who make no scruple in "
      "pronouncing the Automaton a pure machine, unconnected with human agency "
      "in its movements, and consequently, beyond all comparison, the most "
      "astonishing of the inventions of mankind. \n—Edgar Allen Poe",
      // Information Warfare
      "It has been said aforetime that he who knows both sides has nothing to "
      "fear in a hundred fights. \n—Sun Tzu",
      "If the enemy know not where he will be attacked, he must prepare in "
      "every quarter, and so be everywhere weak. \n—Sun Tzu",
      // Global Warming Mitigation
      "Fetter this malefactor to the jagged rocks in adamantine bonds "
      "infrangible. \n—Aeschylus",
      // Cultural Hegemony
      "Ah, my Belovéd, fill the cup that clears to-day of past Regrets and "
      "future Fears\n—To-morrow?—Why, To-morrow I may be Myself with "
      "Yesterday's "
      "Sev'n Thousand Years. \n—Edward FitzGerald",
      // Smart Power Doctrine
      "The time will come and is inevitably coming when all institutions based "
      "on force will disappear through their uselessness, stupidity, and even "
      "inconvenience becoming obvious to all. \n—Leo Tolstoy",
      // Exodus Imperative
      "This, O Best Beloved, is another story of the High and Far Off Times. "
      "\n—Rudyard Kipling",
      // Future Civic
      "You can never plan the future by the past. \n—Edmund Burke",
      "I never think of the future. It comes soon enough. \n—Albert Einstein",
  };

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
                                     "_" + student_id + "_" +
                                     std::to_string(copy_index) + ".jpg";
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

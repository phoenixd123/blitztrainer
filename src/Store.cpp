#include "Store.hpp"

#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace bt {

namespace {
    constexpr int kFormatVersion = 1;
    constexpr char const* kExportPrefix = "BLITZTRAINER1:";

    int64_t geti(matjson::Value const& v, std::string_view k, int64_t def = 0) {
        if (!v.contains(k)) return def;
        return static_cast<int64_t>(v[k].asInt().unwrapOr(def));
    }
    double getd(matjson::Value const& v, std::string_view k, double def = 0) {
        if (!v.contains(k)) return def;
        return v[k].asDouble().unwrapOr(def);
    }
    bool getb(matjson::Value const& v, std::string_view k, bool def = false) {
        if (!v.contains(k)) return def;
        return v[k].asBool().unwrapOr(def);
    }
    std::string gets(matjson::Value const& v, std::string_view k) {
        if (!v.contains(k)) return "";
        return v[k].asString().unwrapOr("");
    }

    std::string sanitize(std::string s) {
        std::string out;
        for (char c : s) {
            if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            else if (!out.empty() && out.back() != '-') out += '-';
        }
        while (!out.empty() && out.back() == '-') out.pop_back();
        if (out.size() > 40) out.resize(40);
        return out.empty() ? "unnamed" : out;
    }
}

matjson::Value toJson(Profile const& p) {
    auto v = matjson::Value::object();
    v["format"] = kFormatVersion;
    v["levelKey"] = p.levelKey;
    v["levelName"] = p.levelName;
    v["levelLength"] = static_cast<double>(p.levelLength);
    v["style"] = static_cast<int>(p.style);
    v["totalAttempts"] = p.totalAttempts;
    v["trackedAttempts"] = p.trackedAttempts;
    v["totalTime"] = p.totalTime;
    v["createdAt"] = p.createdAt;
    v["completedAt"] = p.completedAt;

    auto xs = matjson::Value::array();
    for (float x : p.xs) xs.push(static_cast<double>(x));
    v["xs"] = xs;

    auto deaths = matjson::Value::array();
    for (int d : p.deaths) deaths.push(d);
    v["deaths"] = deaths;

    auto stages = matjson::Value::array();
    for (auto const& st : p.stages) {
        auto ranges = matjson::Value::array();
        for (auto const& r : st.ranges) {
            auto o = matjson::Value::object();
            o["f"] = r.from;
            o["t"] = r.to;
            o["done"] = r.done;
            o["att"] = r.s.attempts;
            o["pass"] = r.s.passes;
            o["streak"] = r.s.streak;
            o["bestStreak"] = r.s.bestStreak;
            o["attFirst"] = r.s.attemptsToFirstPass;
            o["time"] = r.s.timePlayed;
            o["best"] = static_cast<double>(r.s.bestProgress);
            o["firstAt"] = r.s.firstPassAt;
            o["lastAt"] = r.s.lastPassAt;
            ranges.push(o);
        }
        stages.push(ranges);
    }
    v["stages"] = stages;
    return v;
}

Result<Profile> fromJson(matjson::Value const& v) {
    if (!v.isObject()) return Err("Profile is not a JSON object");
    if (geti(v, "format") > kFormatVersion) return Err("Profile was made by a newer version of Blitz Trainer");

    Profile p;
    p.levelKey = gets(v, "levelKey");
    p.levelName = gets(v, "levelName");
    p.levelLength = static_cast<float>(getd(v, "levelLength"));
    p.style = geti(v, "style") == 1 ? StageStyle::Lite : StageStyle::Classic;
    p.totalAttempts = static_cast<int>(geti(v, "totalAttempts"));
    p.trackedAttempts = static_cast<int>(geti(v, "trackedAttempts"));
    p.totalTime = getd(v, "totalTime");
    p.createdAt = geti(v, "createdAt");
    p.completedAt = geti(v, "completedAt");

    if (v.contains("xs")) {
        GEODE_UNWRAP_INTO(auto const& xs, v["xs"].asArray());
        for (auto const& x : xs) p.xs.push_back(static_cast<float>(x.asDouble().unwrapOr(0.0)));
    }
    if (p.xs.size() < 2 || p.levelLength <= 0.f) return Err("Profile has no checkpoints");
    int n = p.sections();

    if (v.contains("deaths")) {
        GEODE_UNWRAP_INTO(auto const& ds, v["deaths"].asArray());
        for (size_t i = 0; i < ds.size() && i < p.deaths.size(); ++i)
            p.deaths[i] = static_cast<int>(ds[i].asInt().unwrapOr(0));
    }

    if (v.contains("stages")) {
        GEODE_UNWRAP_INTO(auto const& stages, v["stages"].asArray());
        for (auto const& sv : stages) {
            Stage st;
            GEODE_UNWRAP_INTO(auto const& ranges, sv.asArray());
            for (auto const& o : ranges) {
                Range r;
                r.from = static_cast<int>(geti(o, "f"));
                r.to = static_cast<int>(geti(o, "t"));
                if (r.from < 0 || r.to > n || r.from >= r.to) return Err("Profile has an invalid range");
                r.done = getb(o, "done");
                r.s.attempts = static_cast<int>(geti(o, "att"));
                r.s.passes = static_cast<int>(geti(o, "pass"));
                r.s.streak = static_cast<int>(geti(o, "streak"));
                r.s.bestStreak = static_cast<int>(geti(o, "bestStreak"));
                r.s.attemptsToFirstPass = static_cast<int>(geti(o, "attFirst"));
                r.s.timePlayed = getd(o, "time");
                r.s.bestProgress = static_cast<float>(getd(o, "best"));
                r.s.firstPassAt = geti(o, "firstAt");
                r.s.lastPassAt = geti(o, "lastAt");
                st.ranges.push_back(r);
            }
            p.stages.push_back(std::move(st));
        }
    }
    if (p.stages.empty()) p.stages = generateStages(n, p.style);
    return Ok(std::move(p));
}

Store& Store::get() {
    static Store s;
    return s;
}

std::filesystem::path Store::pathFor(std::string const& key) const {
    return Mod::get()->getSaveDir() / "profiles" / (key + ".json");
}

void Store::open(std::string const& key, std::string const& levelName) {
    if (key == m_key && m_profile) return; // same level, keep the in-memory profile
    m_key = key;
    m_levelName = levelName;
    m_profile.reset();
    m_undo.reset();
    sessionAttempts = 0;
    sessionPasses = 0;
    sessionTime = 0.0;

    auto path = pathFor(key);
    if (!std::filesystem::exists(path)) return;

    auto text = file::readString(path);
    if (!text) {
        log::error("Could not read {}: {}", path.string(), text.unwrapErr());
        return;
    }
    auto json = matjson::parse(text.unwrap());
    if (!json) {
        log::error("Corrupted profile {}, keeping a copy", path.string());
        std::error_code ec;
        std::filesystem::copy_file(path, path.string() + ".corrupt", std::filesystem::copy_options::overwrite_existing, ec);
        return;
    }
    auto prof = fromJson(json.unwrap());
    if (!prof) {
        log::error("Invalid profile {}: {}", path.string(), prof.unwrapErr());
        return;
    }
    m_profile = std::move(prof.unwrap());
    refreshDone(*m_profile, rules());
}

void Store::close() {
    save();
    m_key.clear();
    m_levelName.clear();
    m_profile.reset();
    m_undo.reset();
}

void Store::setProfile(Profile p) {
    p.levelKey = m_key;
    if (p.levelName.empty()) p.levelName = m_levelName;
    m_profile = std::move(p);
    refreshDone(*m_profile, rules());
    m_undo.reset();
    save();
}

void Store::deleteProfile() {
    m_profile.reset();
    m_undo.reset();
    std::error_code ec;
    auto path = pathFor(m_key);
    if (std::filesystem::exists(path, ec)) {
        // keep a backup so an accidental delete is recoverable by hand
        std::filesystem::copy_file(path, path.string() + ".deleted", std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(path, ec);
    }
}

void Store::save() {
    if (!m_profile || m_key.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(Mod::get()->getSaveDir() / "profiles", ec);
    auto res = file::writeStringSafe(pathFor(m_key), toJson(*m_profile).dump(matjson::NO_INDENTATION));
    if (!res) log::error("Failed to save profile: {}", res.unwrapErr());
}

std::string Store::exportText() const {
    if (!m_profile) return "";
    return kExportPrefix + toJson(*m_profile).dump(matjson::NO_INDENTATION);
}

Result<Profile> Store::importText(std::string const& raw) const {
    std::string text = raw;
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
    size_t start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return Err("The clipboard is empty");
    text = text.substr(start);
    if (text.rfind(kExportPrefix, 0) == 0) text = text.substr(std::char_traits<char>::length(kExportPrefix));
    auto json = matjson::parse(text);
    if (!json) return Err("The clipboard doesn't contain a Blitz Trainer profile");
    return fromJson(json.unwrap());
}

void Store::snapshot() {
    if (m_profile) m_undo = *m_profile;
}

bool Store::undo() {
    if (!m_undo) return false;
    m_profile = std::move(*m_undo);
    m_undo.reset();
    save();
    return true;
}

TrainingRules Store::rules() const {
    TrainingRules r;
    r.requiredPasses = static_cast<int>(std::clamp<int64_t>(Mod::get()->getSettingValue<int64_t>("required-passes"), 1, 20));
    r.consecutive = Mod::get()->getSettingValue<bool>("require-consecutive");
    r.creditAllPassed = Mod::get()->getSettingValue<bool>("credit-all-passed");
    return r;
}

std::string levelKeyFor(GJGameLevel* level) {
    if (!level) return "unknown";
    int id = level->m_levelID.value();
    if (id > 0) return fmt::format("online-{}", id);
    // editor / copied levels have no online ID: key them by name
    // (start position changes are detected separately and can be merged)
    return fmt::format("local-{}", sanitize(level->m_levelName));
}

}

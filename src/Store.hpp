#pragma once

#include <Geode/Geode.hpp>
#include <optional>
#include <string>

#include "core/Model.hpp"

namespace bt {

// Owns the profile of the level that is currently being played and handles
// persistence (one JSON file per level in the mod's save folder).
class Store {
public:
    static Store& get();

    // Loads the profile for `key` (if one was saved) and makes it active.
    void open(std::string const& key, std::string const& levelName);
    void close();

    bool hasProfile() const { return m_profile.has_value(); }
    Profile* profile() { return m_profile ? &*m_profile : nullptr; }
    std::string const& key() const { return m_key; }
    std::string const& levelName() const { return m_levelName; }

    void setProfile(Profile p);
    void deleteProfile();
    void save();

    // Clipboard-friendly export / import
    std::string exportText() const;
    geode::Result<Profile> importText(std::string const& text) const;

    // One-level undo of the last recorded run (in memory only)
    void snapshot();
    bool canUndo() const { return m_undo.has_value(); }
    bool undo();

    TrainingRules rules() const;

    // Session counters, reset when the level is left
    int sessionAttempts = 0;
    int sessionPasses = 0;
    double sessionTime = 0.0;

private:
    std::filesystem::path pathFor(std::string const& key) const;

    std::string m_key;
    std::string m_levelName;
    std::optional<Profile> m_profile;
    std::optional<Profile> m_undo;
};

std::string levelKeyFor(GJGameLevel* level);

matjson::Value toJson(Profile const& p);
geode::Result<Profile> fromJson(matjson::Value const& v);

}

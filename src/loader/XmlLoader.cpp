#include "XmlLoader.h"
#include "Vfs.h"
#include <tinyxml2.h>
#include <iostream>
#include <algorithm>
#include <regex>

namespace HeavyWeapon {

using namespace tinyxml2;

// Helper to sanitize PopCap's XML (multiple root elements and designer typos)
static std::string PrepareXml(const std::string& raw) {
    if (raw.empty()) return "";
    std::string text = raw;

    // Fix PopCap designer typo in Anims.xml: nuke="yes"/ rare="yes">
    size_t typo = text.find("nuke=\"yes\"/ rare=\"yes\">");
    if (typo != std::string::npos) {
        text.replace(typo, 23, "nuke=\"yes\" rare=\"yes\"/>");
    }

    // Strip <?xml ... ?> if present so we can wrap in a single root element
    size_t xmlDecl = text.find("<?xml");
    if (xmlDecl != std::string::npos) {
        size_t declEnd = text.find("?>", xmlDecl);
        if (declEnd != std::string::npos) {
            text.erase(xmlDecl, declEnd - xmlDecl + 2);
        }
    }

    return "<Root>" + text + "</Root>";
}

bool XmlLoader::LoadCrafts(const std::string& path, std::unordered_map<std::string, CraftDef>& outCrafts) {
    std::string raw = Vfs::ReadTextFile(path);
    if (raw.empty()) {
        std::cerr << "[XmlLoader] Failed to read craft file: " << path << std::endl;
        return false;
    }

    std::string prepared = PrepareXml(raw);
    XMLDocument doc;
    if (doc.Parse(prepared.c_str()) != XML_SUCCESS) {
        std::cerr << "[XmlLoader] Failed to parse craft XML: " << path << std::endl;
        return false;
    }

    XMLElement* root = doc.FirstChildElement("Root");
    if (!root) return false;

    outCrafts.clear();
    int nextId = 1;   // order matters: craft.xml says "Do NOT change the order"
    for (XMLElement* elem = root->FirstChildElement("Craft"); elem != nullptr; elem = elem->NextSiblingElement("Craft")) {
        CraftDef craft;
        craft.id = nextId++;
        const char* name = elem->Attribute("name");
        const char* desc = elem->Attribute("desc");
        const char* arms = elem->Attribute("arms");
        if (name) craft.name = name;
        if (desc) craft.desc = desc;
        if (arms) craft.arms = arms;
        elem->QueryIntAttribute("points", &craft.points);
        elem->QueryIntAttribute("armor", &craft.armor);

        if (!craft.name.empty()) {
            outCrafts[craft.name] = craft;
        }
    }
    std::cout << "[XmlLoader] Loaded " << outCrafts.size() << " craft definitions." << std::endl;
    return true;
}

bool XmlLoader::LoadLevels(const std::string& path, std::vector<LevelDef>& outLevels) {
    std::string raw = Vfs::ReadTextFile(path);
    if (raw.empty()) {
        std::cerr << "[XmlLoader] Failed to read levels file: " << path << std::endl;
        return false;
    }

    std::string prepared = PrepareXml(raw);
    XMLDocument doc;
    if (doc.Parse(prepared.c_str()) != XML_SUCCESS) {
        std::cerr << "[XmlLoader] Failed to parse levels XML: " << path << std::endl;
        return false;
    }

    XMLElement* root = doc.FirstChildElement("Root");
    if (!root) return false;

    std::vector<LevelDef> regions;
    for (XMLElement* elem = root->FirstChildElement("Level"); elem != nullptr; elem = elem->NextSiblingElement("Level")) {
        LevelDef reg;
        const char* name = elem->Attribute("name");
        if (name) reg.name = name;
        elem->QueryIntAttribute("length", &reg.length);

        std::string lowerName = reg.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        reg.bgTheme = lowerName;

        for (XMLElement* intel = elem->FirstChildElement("Intel"); intel != nullptr; intel = intel->NextSiblingElement("Intel")) {
            const char* text = intel->Attribute("text");
            if (text) {
                reg.intelList.push_back({ text });
            }
        }
        regions.push_back(reg);
    }

    // Heavy Weapon campaign consists of 19 missions across these 10 regions:
    // Missions 1-9: Frigistan to Killingrad (First campaign sweep)
    // Missions 10-18: Frigistan to Killingrad (Second campaign sweep - increased enemy waves and Tier 2 bosses)
    // Mission 19: Red Star HQ (Final fortress)
    outLevels.clear();
    for (int i = 0; i < NUM_CAMPAIGN_MISSIONS; ++i) {
        int regIdx = (i < 9) ? i : (i < 18) ? (i - 9) : (int)regions.size() - 1;
        if (regIdx < (int)regions.size()) {
            LevelDef mission = regions[regIdx];
            outLevels.push_back(mission);
        }
    }
    std::cout << "[XmlLoader] Configured " << outLevels.size() << " campaign levels from " << regions.size() << " regions." << std::endl;
    return true;
}

bool XmlLoader::LoadWaves(const std::string& path, std::vector<LevelDef>& inOutLevels) {
    std::string raw = Vfs::ReadTextFile(path);
    if (raw.empty()) {
        std::cerr << "[XmlLoader] Failed to read waves file: " << path << std::endl;
        return false;
    }

    std::string prepared = PrepareXml(raw);
    XMLDocument doc;
    if (doc.Parse(prepared.c_str()) != XML_SUCCESS) {
        std::cerr << "[XmlLoader] Failed to parse waves XML: " << path << std::endl;
        return false;
    }

    XMLElement* root = doc.FirstChildElement("Root");
    if (!root) return false;

    size_t levelIdx = 0;
    for (XMLElement* lvlElem = root->FirstChildElement("Level"); lvlElem != nullptr && levelIdx < inOutLevels.size();
         lvlElem = lvlElem->NextSiblingElement("Level"), levelIdx++) {
        
        inOutLevels[levelIdx].waves.clear();
        for (XMLElement* wElem = lvlElem->FirstChildElement("Wave"); wElem != nullptr; wElem = wElem->NextSiblingElement("Wave")) {
            WaveDef wave;
            wElem->QueryIntAttribute("length", &wave.length);

            for (XMLElement* cElem = wElem->FirstChildElement("Craft"); cElem != nullptr; cElem = cElem->NextSiblingElement("Craft")) {
                const char* id = cElem->Attribute("id");
                int qty = 1;
                cElem->QueryIntAttribute("qty", &qty);
                if (id) {
                    wave.craftList.push_back({ id, qty });
                }
            }
            inOutLevels[levelIdx].waves.push_back(wave);
        }
    }
    std::cout << "[XmlLoader] Assigned wave data to " << levelIdx << " levels." << std::endl;
    return true;
}

bool XmlLoader::LoadSurvival(const std::string& path, std::vector<LevelDef>& outTiers) {
    std::string raw = Vfs::ReadTextFile(path);
    if (raw.empty()) return false;
    XMLDocument doc;
    if (doc.Parse(PrepareXml(raw).c_str()) != XML_SUCCESS) return false;
    XMLElement* root = doc.FirstChildElement("Root");
    if (!root) return false;
    outTiers.clear();
    for (XMLElement* lvl = root->FirstChildElement("Level"); lvl; lvl = lvl->NextSiblingElement("Level")) {
        LevelDef def;
        for (XMLElement* wElem = lvl->FirstChildElement("Wave"); wElem; wElem = wElem->NextSiblingElement("Wave")) {
            WaveDef wave;
            wElem->QueryIntAttribute("length", &wave.length);
            for (XMLElement* c = wElem->FirstChildElement("Craft"); c; c = c->NextSiblingElement("Craft")) {
                const char* id = c->Attribute("id");
                int qty = 1;
                c->QueryIntAttribute("qty", &qty);
                if (id) wave.craftList.push_back({ id, qty });
            }
            def.waves.push_back(wave);
        }
        outTiers.push_back(std::move(def));
    }
    return !outTiers.empty();
}

bool XmlLoader::LoadBosses(const std::string& path, std::unordered_map<std::string, BossDef>& outBosses) {
    std::string raw = Vfs::ReadTextFile(path);
    if (raw.empty()) {
        std::cerr << "[XmlLoader] Failed to read bosses file: " << path << std::endl;
        return false;
    }

    std::string prepared = PrepareXml(raw);
    XMLDocument doc;
    if (doc.Parse(prepared.c_str()) != XML_SUCCESS) {
        std::cerr << "[XmlLoader] Failed to parse bosses XML: " << path << std::endl;
        return false;
    }

    XMLElement* root = doc.FirstChildElement("Root");
    if (!root) return false;

    outBosses.clear();
    for (XMLElement* bossElem = root->FirstChildElement(); bossElem != nullptr; bossElem = bossElem->NextSiblingElement()) {
        BossDef boss;
        boss.type = bossElem->Name();
        const char* info = bossElem->Attribute("info");
        if (info) boss.info = info;

        for (int lvl = 1; lvl <= 3; ++lvl) {
            std::string lvlTag = "Level" + std::to_string(lvl);
            XMLElement* lvlElem = bossElem->FirstChildElement(lvlTag.c_str());
            if (!lvlElem && lvl == 1) lvlElem = bossElem->FirstChildElement("Level");   // Final has one level
            if (!lvlElem) continue;

            BossLevelDef bLevel;
            lvlElem->QueryIntAttribute("armor", &bLevel.armor);
            lvlElem->QueryIntAttribute("score", &bLevel.score);
            lvlElem->QueryIntAttribute("turretarmor", &bLevel.turretArmor);
            lvlElem->QueryIntAttribute("fire", &bLevel.fireInterval);

            // Turrets
            // "Turret" (helicopter) or numbered "Turret1".."Turret4" (battleship).
            for (XMLElement* tElem = lvlElem->FirstChildElement(); tElem != nullptr; tElem = tElem->NextSiblingElement()) {
                if (std::string(tElem->Name()).rfind("Turret", 0) != 0) continue;
                BossTurretDef turret;
                tElem->QueryIntAttribute("armor", &turret.armor);
                tElem->QueryIntAttribute("fire", &turret.fireInterval);
                tElem->QueryFloatAttribute("speed", &turret.speed);
                const char* stat = tElem->Attribute("stationary");
                turret.stationary = (stat && std::string(stat) == "yes");
                bLevel.turrets.push_back(turret);
            }

            // Launchers
            // "Launcher" (helicopter) or numbered "Launcher1", "Launcher2" (head).
            for (XMLElement* lElem = lvlElem->FirstChildElement(); lElem != nullptr; lElem = lElem->NextSiblingElement()) {
                if (std::string(lElem->Name()).rfind("Launcher", 0) != 0) continue;
                BossLauncherDef launcher;
                lElem->QueryIntAttribute("armor", &launcher.armor);
                lElem->QueryIntAttribute("fire", &launcher.fireInterval);
                lElem->QueryFloatAttribute("speed", &launcher.speed);
                bLevel.launchers.push_back(launcher);
            }

            // Dishes / Special weapons
            XMLElement* dishElem = lvlElem->FirstChildElement("Dish");
            if (dishElem) {
                dishElem->QueryIntAttribute("down", &bLevel.dishDown);
                dishElem->QueryIntAttribute("up", &bLevel.dishUp);
                dishElem->QueryIntAttribute("meteors", &bLevel.dishMeteors);
            }
            if (const char* lc = lvlElem->Attribute("longchain")) bLevel.longChain = std::string(lc) == "yes";
            lvlElem->QueryDoubleAttribute("throw", &bLevel.throwSpeed);
            if (XMLElement* e = lvlElem->FirstChildElement("Laser")) e->QueryIntAttribute("fire", &bLevel.laserFire);
            if (XMLElement* e = lvlElem->FirstChildElement("SmallLauncher")) e->QueryIntAttribute("fire", &bLevel.smallLauncherFire);
            if (XMLElement* e = lvlElem->FirstChildElement("BigLauncher")) e->QueryIntAttribute("fire", &bLevel.bigLauncherFire);
            lvlElem->QueryDoubleAttribute("speed", &bLevel.speed);
            lvlElem->QueryIntAttribute("boulders", &bLevel.boulders);
            for (auto [name, jump] : { std::pair<const char*, BossLevelDef::Jump*>{ "Jump", &bLevel.wormJump }, { "JumpFast", &bLevel.wormJumpFast } }) {
                if (XMLElement* jElem = lvlElem->FirstChildElement(name)) {
                    jElem->QueryIntAttribute("depth", &jump->depth);
                    jElem->QueryDoubleAttribute("jump", &jump->jump);
                    jElem->QueryDoubleAttribute("gravity", &jump->gravity);
                    jElem->QueryDoubleAttribute("lateral", &jump->lateral);
                }
            }
            if (XMLElement* cElem = lvlElem->FirstChildElement("Charge")) cElem->QueryIntAttribute("delay", &bLevel.chargeDelay);
            if (XMLElement* bElem = lvlElem->FirstChildElement("Bomb")) {
                bElem->QueryIntAttribute("on", &bLevel.bombOn);
                bElem->QueryIntAttribute("off", &bLevel.bombOff);
                bElem->QueryIntAttribute("freq", &bLevel.bombFreq);
                bElem->QueryIntAttribute("fire", &bLevel.bombFire);
            }
            if (XMLElement* hElem = lvlElem->FirstChildElement("Hand")) {
                hElem->QueryIntAttribute("armor", &bLevel.handArmor);
                hElem->QueryIntAttribute("fire", &bLevel.handFire);
            }
            if (XMLElement* aElem = lvlElem->FirstChildElement("Arm")) aElem->QueryIntAttribute("armor", &bLevel.armArmor);
            if (XMLElement* jElem = lvlElem->FirstChildElement("Jump")) {
                jElem->QueryDoubleAttribute("downaccel", &bLevel.downAccel);
                jElem->QueryDoubleAttribute("downmax", &bLevel.downMax);
                jElem->QueryDoubleAttribute("upaccel", &bLevel.upAccel);
                jElem->QueryDoubleAttribute("upmax", &bLevel.upMax);
                jElem->QueryDoubleAttribute("xspeed", &bLevel.jumpX);
                jElem->QueryDoubleAttribute("yspeed", &bLevel.jumpY);
                jElem->QueryDoubleAttribute("gravity", &bLevel.jumpGravity);
            }

            boss.levels.push_back(bLevel);
        }
        outBosses[boss.type] = boss;
    }
    std::cout << "[XmlLoader] Loaded " << outBosses.size() << " boss definitions." << std::endl;
    return true;
}

bool XmlLoader::LoadAnims(const std::string& path, std::vector<std::vector<AnimDef>>& outLevelAnims) {
    std::string raw = Vfs::ReadTextFile(path);
    if (raw.empty()) {
        std::cerr << "[XmlLoader] Failed to read anims file: " << path << std::endl;
        return false;
    }

    std::string prepared = PrepareXml(raw);
    XMLDocument doc;
    if (doc.Parse(prepared.c_str()) != XML_SUCCESS) {
        std::cerr << "[XmlLoader] Failed to parse anims XML: " << path << std::endl;
        return false;
    }

    XMLElement* root = doc.FirstChildElement("Root");
    if (!root) return false;

    outLevelAnims.clear();
    for (XMLElement* lvlElem = root->FirstChildElement("Level"); lvlElem != nullptr; lvlElem = lvlElem->NextSiblingElement("Level")) {
        std::vector<AnimDef> anims;
        for (XMLElement* aElem = lvlElem->FirstChildElement("Anim"); aElem != nullptr; aElem = aElem->NextSiblingElement("Anim")) {
            AnimDef anim;
            const char* name = aElem->Attribute("name");
            const char* type = aElem->Attribute("type");
            if (name) anim.name = name;
            if (type) anim.type = type;

            aElem->QueryIntAttribute("frames", &anim.frames);
            aElem->QueryFloatAttribute("speed", &anim.speed);
            aElem->QueryIntAttribute("plane", &anim.plane);
            aElem->QueryIntAttribute("offset", &anim.offset);
            aElem->QueryIntAttribute("y", &anim.y);
            aElem->QueryFloatAttribute("mx", &anim.mx);

            const char* nuke = aElem->Attribute("nuke");
            anim.nuke = (nuke && std::string(nuke) == "yes");

            const char* rare = aElem->Attribute("rare");
            anim.rare = (rare && std::string(rare) == "yes");

            for (XMLElement* dElem = aElem->FirstChildElement("Delay"); dElem != nullptr; dElem = dElem->NextSiblingElement("Delay")) {
                AnimDelayDef delay;
                dElem->QueryIntAttribute("frame", &delay.frame);
                dElem->QueryFloatAttribute("speed", &delay.speed);
                anim.delays.push_back(delay);
            }
            anims.push_back(anim);
        }
        outLevelAnims.push_back(anims);
    }
    std::cout << "[XmlLoader] Loaded anims for " << outLevelAnims.size() << " levels." << std::endl;
    return true;
}

} // namespace HeavyWeapon

#pragma once
#include <string>
#include <string_view>
namespace stackchan {
inline std::string NormalizeEmotion(std::string_view name) {
    for (auto known :
         {"neutral", "happy",       "laughing",  "funny",     "sad",      "angry",   "crying",
          "loving",  "embarrassed", "surprised", "shocked",   "thinking", "winking", "cool",
          "relaxed", "delicious",   "kissy",     "confident", "sleepy",   "silly",   "confused"})
        if (name == known)
            return std::string(name);
    return "neutral";
}
inline std::string FaceAsset(std::string_view name, bool speaking) {
    auto emotion = NormalizeEmotion(name);
    return speaking && emotion != "sleepy" ? emotion + "_talk" : emotion;
}
}  // namespace stackchan

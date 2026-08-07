#pragma once
#include <string>
#include <vector>
namespace progressive {
struct SasEmojiData { std::string emoji; std::string name; };
std::vector<SasEmojiData> getSasEmojiList();
SasEmojiData getSasEmojiByIndex(int idx);
std::string formatSasEmojiDisplay(const std::vector<int>& indices);
}

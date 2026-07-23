// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ayu/data/entities.h"

class History;
class HistoryItem;

namespace AyuMapper {

constexpr auto kDocumentTypeNone = 0;
constexpr auto kDocumentTypePhoto = 1;
constexpr auto kDocumentTypeDocument = 2;

template<typename MTPObject>
[[nodiscard]] MTPObject deserializeObject(std::vector<char> serialized);

template<typename MTPObject>
[[nodiscard]] std::vector<char> serializeObject(MTPObject object);

std::pair<std::string, std::vector<char>> serializeTextWithEntities(not_null<HistoryItem*> item);
[[nodiscard]] MTPVector<MTPMessageEntity> deserializeTextWithEntities(std::vector<char> serialized);
int mapItemFlagsToMTPFlags(not_null<HistoryItem*> item);
void mapMediaToMessage(
	not_null<HistoryItem*> item,
	AyuMessageBase &message);
[[nodiscard]] bool hasStoredMedia(const AyuMessageBase &message);
[[nodiscard]] MTPMessageMedia mediaFromMessage(
	const AyuMessageBase &message,
	not_null<History*> history);

} // namespace AyuMapper

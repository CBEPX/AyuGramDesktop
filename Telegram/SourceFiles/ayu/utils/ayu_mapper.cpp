// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/utils/ayu_mapper.h"

#include "api/api_text_entities.h"
#include "apiwrap.h"
#include "core/file_location.h"
#include "data/stickers/data_stickers.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "main/main_session.h"
#include "mtproto/connection_abstract.h"
#include "mtproto/details/mtproto_dump_to_text.h"
#include "ui/image/image_location.h"

#include <QtCore/QFileInfo>

#include <cstring>

namespace AyuMapper {

constexpr auto kMessageFlagUnread = 0x00000001;
constexpr auto kMessageFlagOut = 0x00000002;
constexpr auto kMessageFlagForwarded = 0x00000004;
constexpr auto kMessageFlagReply = 0x00000008;
constexpr auto kMessageFlagMention = 0x00000010;
constexpr auto kMessageFlagContentUnread = 0x00000020;
constexpr auto kMessageFlagHasMarkup = 0x00000040;
constexpr auto kMessageFlagHasEntities = 0x00000080;
constexpr auto kMessageFlagHasFromId = 0x00000100;
constexpr auto kMessageFlagHasMedia = 0x00000200;
constexpr auto kMessageFlagHasViews = 0x00000400;
constexpr auto kMessageFlagHasBotId = 0x00000800;
constexpr auto kMessageFlagIsSilent = 0x00001000;
constexpr auto kMessageFlagIsPost = 0x00004000;
constexpr auto kMessageFlagEdited = 0x00008000;
constexpr auto kMessageFlagHasPostAuthor = 0x00010000;
constexpr auto kMessageFlagIsGrouped = 0x00020000;
constexpr auto kMessageFlagFromScheduled = 0x00040000;
constexpr auto kMessageFlagHasReactions = 0x00100000;
constexpr auto kMessageFlagHideEdit = 0x00200000;
constexpr auto kMessageFlagRestricted = 0x00400000;
constexpr auto kMessageFlagHasReplies = 0x00800000;
constexpr auto kMessageFlagIsPinned = 0x01000000;
constexpr auto kMessageFlagHasTTL = 0x02000000;
constexpr auto kMessageFlagInvertMedia = 0x08000000;
constexpr auto kMessageFlagHasSavedPeer = 0x10000000;

namespace {

[[nodiscard]] std::optional<MTPstring> SizeTypeFromLocation(
		const ImageLocation &location,
		UserId self) {
	const auto storage = std::get_if<StorageFileLocation>(
		&location.file().data);
	if (!storage || !storage->valid()) {
		return std::nullopt;
	}

	auto result = std::optional<MTPstring>();
	storage->tl(self).match([&](const MTPDinputPhotoFileLocation &data) {
		if (!data.vthumb_size().v.isEmpty()) {
			result = data.vthumb_size();
		}
	}, [&](const MTPDinputDocumentFileLocation &data) {
		if (!data.vthumb_size().v.isEmpty()) {
			result = data.vthumb_size();
		}
	}, [&](const auto &) {
	});
	return result;
}

[[nodiscard]] std::optional<MTPPhotoSize> PhotoSizeFromLocation(
		const ImageLocation &location,
		int byteSize,
		UserId self) {
	const auto type = SizeTypeFromLocation(location, self);
	if (!type || location.width() <= 0 || location.height() <= 0) {
		return std::nullopt;
	}
	return MTP_photoSize(
		*type,
		MTP_int(location.width()),
		MTP_int(location.height()),
		MTP_int(byteSize));
}

[[nodiscard]] std::optional<MTPVideoSize> VideoSizeFromLocation(
		const ImageLocation &location,
		int byteSize,
		crl::time start,
		UserId self) {
	const auto type = SizeTypeFromLocation(location, self);
	if (!type || location.width() <= 0 || location.height() <= 0) {
		return std::nullopt;
	}

	using Flag = MTPDvideoSize::Flag;
	auto flags = MTPDvideoSize::Flags();
	if (start > 0) {
		flags |= Flag::f_video_start_ts;
	}
	return MTP_videoSize(
		MTP_flags(flags),
		*type,
		MTP_int(location.width()),
		MTP_int(location.height()),
		MTP_int(byteSize),
		MTP_double(start / 1000.));
}

[[nodiscard]] QVector<MTPDocumentAttribute> DocumentAttributes(
		not_null<DocumentData*> document) {
	auto result = QVector<MTPDocumentAttribute>();
	const auto filename = document->filename();
	if (!filename.isEmpty()) {
		result.push_back(MTP_documentAttributeFilename(MTP_string(filename)));
	}

	const auto dimensions = document->dimensions;
	const auto sticker = document->sticker();
	const auto hasVideoAttribute = document->isVideoFile()
		|| document->isGifv()
		|| document->isVideoMessage()
		|| (sticker && sticker->isWebm());
	if (hasVideoAttribute) {
		using Flag = MTPDdocumentAttributeVideo::Flag;
		auto flags = MTPDdocumentAttributeVideo::Flags();
		if (document->isVideoMessage()) {
			flags |= Flag::f_round_message;
		}
		if (document->supportsStreaming()) {
			flags |= Flag::f_supports_streaming;
		}
		if (document->isSilentVideo()) {
			flags |= Flag::f_nosound;
		}
		const auto preload = document->videoPreloadPrefix();
		if (preload > 0) {
			flags |= Flag::f_preload_prefix_size;
		}
		const auto video = document->video();
		if (video && !video->codec.isEmpty()) {
			flags |= Flag::f_video_codec;
		}
		result.push_back(MTP_documentAttributeVideo(
			MTP_flags(flags),
			MTP_double(document->duration() / 1000.),
			MTP_int(dimensions.width()),
			MTP_int(dimensions.height()),
			MTP_int(preload),
			MTPdouble(),
			video ? MTP_string(video->codec) : MTPstring()));
	} else if (dimensions.width() > 0 && dimensions.height() > 0) {
		result.push_back(MTP_documentAttributeImageSize(
			MTP_int(dimensions.width()),
			MTP_int(dimensions.height())));
	}

	if (document->isAnimation()) {
		result.push_back(MTP_documentAttributeAnimated());
	}

	if (sticker) {
		if (sticker->setType == Data::StickersType::Emoji) {
			using Flag = MTPDdocumentAttributeCustomEmoji::Flag;
			auto flags = MTPDdocumentAttributeCustomEmoji::Flags();
			if (!document->isPremiumEmoji()) {
				flags |= Flag::f_free;
			}
			if (document->emojiUsesTextColor()) {
				flags |= Flag::f_text_color;
			}
			result.push_back(MTP_documentAttributeCustomEmoji(
				MTP_flags(flags),
				MTP_string(sticker->alt),
				Data::InputStickerSet(sticker->set)));
		} else {
			using Flag = MTPDdocumentAttributeSticker::Flag;
			auto flags = MTPDdocumentAttributeSticker::Flags();
			if (sticker->setType == Data::StickersType::Masks) {
				flags |= Flag::f_mask;
			}
			result.push_back(MTP_documentAttributeSticker(
				MTP_flags(flags),
				MTP_string(sticker->alt),
				Data::InputStickerSet(sticker->set),
				MTPMaskCoords()));
		}
	} else if (const auto song = document->song()) {
		using Flag = MTPDdocumentAttributeAudio::Flag;
		auto flags = MTPDdocumentAttributeAudio::Flags();
		if (!song->title.isEmpty()) {
			flags |= Flag::f_title;
		}
		if (!song->performer.isEmpty()) {
			flags |= Flag::f_performer;
		}
		result.push_back(MTP_documentAttributeAudio(
			MTP_flags(flags),
			MTP_int(document->duration() / 1000),
			MTP_string(song->title),
			MTP_string(song->performer),
			MTPbytes()));
	} else if (const auto voice = document->voice()) {
		using Flag = MTPDdocumentAttributeAudio::Flag;
		auto flags = MTPDdocumentAttributeAudio::Flags();
		flags |= Flag::f_voice;
		if (!voice->waveform.isEmpty()) {
			flags |= Flag::f_waveform;
		}
		result.push_back(MTP_documentAttributeAudio(
			MTP_flags(flags),
			MTP_int(document->duration() / 1000),
			MTPstring(),
			MTPstring(),
			voice->waveform.isEmpty()
				? MTPbytes()
				: MTP_bytes(documentWaveformEncode5bit(voice->waveform))));
	}

	if (document->hasAttachedStickers()) {
		result.push_back(MTP_documentAttributeHasStickers());
	}
	return result;
}

[[nodiscard]] std::optional<MTPPhoto> PhotoFromData(
		not_null<PhotoData*> photo,
		UserId self) {
	auto result = std::optional<MTPPhoto>();
	photo->mtpInput().match([&](const MTPDinputPhoto &input) {
		auto sizes = QVector<MTPPhotoSize>();
		const auto inlineThumbnail = photo->inlineThumbnailBytes();
		if (!inlineThumbnail.isEmpty()) {
			sizes.push_back(MTP_photoStrippedSize(
				MTP_string("i"),
				MTP_bytes(inlineThumbnail)));
		}

		auto hasRegularSize = false;
		for (const auto size : {
				Data::PhotoSize::Small,
				Data::PhotoSize::Thumbnail,
				Data::PhotoSize::Large,
			}) {
			if (!photo->hasExact(size)) {
				continue;
			}
			const auto mapped = PhotoSizeFromLocation(
				photo->location(size),
				photo->imageByteSize(size),
				self);
			if (mapped) {
				sizes.push_back(*mapped);
				hasRegularSize = true;
			}
		}
		if (!hasRegularSize) {
			return;
		}

		auto videoSizes = QVector<MTPVideoSize>();
		if (photo->hasVideoSmall()) {
			const auto mapped = VideoSizeFromLocation(
				photo->videoLocation(Data::PhotoSize::Small),
				photo->videoByteSize(Data::PhotoSize::Small),
				photo->videoStartPosition(),
				self);
			if (mapped) {
				videoSizes.push_back(*mapped);
			}
		}
		if (photo->hasVideo()) {
			const auto mapped = VideoSizeFromLocation(
				photo->videoLocation(Data::PhotoSize::Large),
				photo->videoByteSize(Data::PhotoSize::Large),
				photo->videoStartPosition(),
				self);
			if (mapped) {
				videoSizes.push_back(*mapped);
			}
		}

		using Flag = MTPDphoto::Flag;
		auto flags = MTPDphoto::Flags();
		if (photo->hasAttachedStickers()) {
			flags |= Flag::f_has_stickers;
		}
		if (!videoSizes.empty()) {
			flags |= Flag::f_video_sizes;
		}
		result = MTP_photo(
			MTP_flags(flags),
			MTP_long(photo->id),
			MTP_long(input.vaccess_hash().v),
			MTP_bytes(input.vfile_reference().v),
			MTP_int(photo->date()),
			MTP_vector<MTPPhotoSize>(sizes),
			MTP_vector<MTPVideoSize>(videoSizes),
			MTP_int(photo->getDC()));
	}, [&](const MTPDinputPhotoEmpty &) {
	});
	return result;
}

[[nodiscard]] std::optional<MTPDocument> DocumentFromData(
		not_null<DocumentData*> document,
		UserId self) {
	if (!document->hasRemoteLocation()) {
		return std::nullopt;
	}

	auto result = std::optional<MTPDocument>();
	document->mtpInput().match([&](const MTPDinputDocument &input) {
		auto thumbs = QVector<MTPPhotoSize>();
		const auto inlineThumbnail = document->inlineThumbnailBytes();
		if (!inlineThumbnail.isEmpty()) {
			thumbs.push_back(document->inlineThumbnailIsPath()
				? MTP_photoPathSize(
					MTP_string("i"),
					MTP_bytes(inlineThumbnail))
				: MTP_photoStrippedSize(
					MTP_string("i"),
					MTP_bytes(inlineThumbnail)));
		}
		if (document->hasThumbnail()) {
			const auto mapped = PhotoSizeFromLocation(
				document->thumbnailLocation(),
				document->thumbnailByteSize(),
				self);
			if (mapped) {
				thumbs.push_back(*mapped);
			}
		}

		auto videoThumbs = QVector<MTPVideoSize>();
		if (document->hasVideoThumbnail()) {
			const auto mapped = VideoSizeFromLocation(
				document->videoThumbnailLocation(),
				document->videoThumbnailByteSize(),
				0,
				self);
			if (mapped) {
				videoThumbs.push_back(*mapped);
			}
		}

		using Flag = MTPDdocument::Flag;
		auto flags = MTPDdocument::Flags();
		if (!thumbs.empty()) {
			flags |= Flag::f_thumbs;
		}
		if (!videoThumbs.empty()) {
			flags |= Flag::f_video_thumbs;
		}
		result = MTP_document(
			MTP_flags(flags),
			MTP_long(document->id),
			MTP_long(input.vaccess_hash().v),
			MTP_bytes(input.vfile_reference().v),
			MTP_int(document->date),
			MTP_string(document->mimeString()),
			MTP_long(document->size),
			MTP_vector<MTPPhotoSize>(thumbs),
			MTP_vector<MTPVideoSize>(videoThumbs),
			MTP_int(document->getDC()),
			MTP_vector<MTPDocumentAttribute>(
				DocumentAttributes(document)));
	}, [&](const MTPDinputDocumentEmpty &) {
	});
	return result;
}

void StoreMediaLocation(
		const Core::FileLocation &location,
		AyuMessageBase &message) {
	if (location.isEmpty() || location.inMediaCache()) {
		return;
	}
	const auto path = location.name();
	const auto info = QFileInfo(path);
	const auto checked = Core::FileLocation(path);
	if (path == u"/"_q || !info.isFile() || !checked.check()) {
		return;
	}
	message.mediaPath = path.toStdString();
}

template <typename Object>
[[nodiscard]] std::optional<Object> ReadStoredMedia(
		const std::vector<char> &serialized) {
	if (serialized.empty()
		|| (serialized.size() % sizeof(mtpPrime)) != 0) {
		return std::nullopt;
	}

	auto buffer = mtpBuffer(serialized.size() / sizeof(mtpPrime));
	std::memcpy(buffer.data(), serialized.data(), serialized.size());
	const auto *from = buffer.data();
	const auto *end = from + buffer.size();
	auto object = Object();
	return (object.read(from, end) && from == end)
		? std::optional<Object>(std::move(object))
		: std::nullopt;
}

template <typename Media>
void RestoreMediaLocation(
		const AyuMessageBase &message,
		not_null<Media*> media) {
	const auto path = QString::fromStdString(message.mediaPath);
	if (path.isEmpty() || path == u"/"_q || !QFileInfo(path).isFile()) {
		return;
	}
	const auto location = Core::FileLocation(path);
	if (location.inMediaCache() || !location.check()) {
		return;
	}
	media->setLocation(location);
}

} // namespace

template<typename MTPObject>
std::vector<char> serializeObject(MTPObject object) {
	mtpBuffer buffer;
	object.write(buffer);

	const auto from = reinterpret_cast<char*>(buffer.data());
	const auto end = from + buffer.size() * sizeof(mtpPrime);

	std::vector<char> entities(from, end);
	return entities;
}

template<typename MTPObject>
MTPObject deserializeObject(std::vector<char> serialized) {
	gsl::span<char> span(serialized.data(), serialized.size());

	auto from = reinterpret_cast<const mtpPrime*>(span.data());
	const auto end = from + span.size() / sizeof(mtpPrime);

	MTPObject data;
	if (!data.read(from, end)) {
		LOG(("AyuMapper: Failed to deserialize object"));
	}
	return data;
}

std::pair<std::string, std::vector<char>> serializeTextWithEntities(not_null<HistoryItem*> item) {
	if (item->emptyText()) {
		return std::make_pair("", std::vector<char>());
	}
	auto textWithEntities = item->originalText();


	std::vector<char> entities;
	if (!textWithEntities.entities.empty()) {
		const auto mtpEntities = Api::EntitiesToMTP(
			&item->history()->session(),
			textWithEntities.entities,
			Api::ConvertOption::WithLocal);

		entities = serializeObject(mtpEntities);
	}

	return std::make_pair(textWithEntities.text.toStdString(), entities);
}

MTPVector<MTPMessageEntity> deserializeTextWithEntities(std::vector<char> serialized) {
	return deserializeObject<MTPVector<MTPMessageEntity>>(serialized);
}

int mapItemFlagsToMTPFlags(not_null<HistoryItem*> item) {
	int flags = 0;

	const auto thread = item->topic()
							? reinterpret_cast<Data::Thread*>(item->topic())
							: item->history();
	if (item->unread(thread)) {
		flags |= kMessageFlagUnread;
	}

	if (item->out()) {
		flags |= kMessageFlagOut;
	}

	if (item->Get<HistoryMessageForwarded>()) {
		flags |= kMessageFlagForwarded;
	}

	if (item->Get<HistoryMessageReply>()) {
		flags |= kMessageFlagReply;
	}

	if (item->mentionsMe()) {
		flags |= kMessageFlagMention;
	}

	if (item->hasUnreadMediaFlag()) {
		flags |= kMessageFlagContentUnread;
	}

	if (item->definesReplyKeyboard()) {
		flags |= kMessageFlagHasMarkup;
	}

	if (!item->originalText().entities.empty()) {
		flags |= kMessageFlagHasEntities;
	}

	if (item->displayFrom()) {
		// todo: maybe wrong
		flags |= kMessageFlagHasFromId;
	}

	if (item->media()) {
		flags |= kMessageFlagHasMedia;
	}

	if (item->hasViews()) {
		flags |= kMessageFlagHasViews;
	}

	if (item->viaBot()) {
		flags |= kMessageFlagHasBotId;
	}

	if (item->isSilent()) {
		flags |= kMessageFlagIsSilent;
	}

	if (item->isPost()) {
		flags |= kMessageFlagIsPost;
	}

	if (item->Get<HistoryMessageEdited>()) {
		flags |= kMessageFlagEdited;
	}

	if (item->Get<HistoryMessageSigned>()) {
		flags |= kMessageFlagHasPostAuthor;
	}

	if (item->groupId()) {
		flags |= kMessageFlagIsGrouped;
	}

	if (item->isScheduled()) {
		flags |= kMessageFlagFromScheduled;
	}

	if (!item->reactions().empty()) {
		flags |= kMessageFlagHasReactions;
	}

	if (item->hideEditedBadge()) {
		flags |= kMessageFlagHideEdit;
	}

	if (item->hasPossibleRestrictions()) {
		flags |= kMessageFlagRestricted;
	}

	if (item->repliesCount() > 0) {
		flags |= kMessageFlagHasReplies;
	}

	if (item->isPinned()) {
		flags |= kMessageFlagIsPinned;
	}

	if (item->ttlDestroyAt() > 0) {
		flags |= kMessageFlagHasTTL;
	}

	if (item->invertMedia()) {
		flags |= kMessageFlagInvertMedia;
	}

	if (item->savedFromSender()) {
		// todo: maybe wrong
		flags |= kMessageFlagHasSavedPeer;
	}

	return flags;
}

void mapMediaToMessage(
		not_null<HistoryItem*> item,
		AyuMessageBase &message) {
	message.mediaPath.clear();
	message.hqThumbPath.clear();
	message.documentType = kDocumentTypeNone;
	message.documentSerialized.clear();
	message.thumbsSerialized.clear();
	message.documentAttributesSerialized.clear();
	message.mimeType.clear();

	const auto media = item->media();
	if (!media) {
		return;
	}
	const auto self = item->history()->session().userId();
	if (const auto photo = media->photo()) {
		const auto stored = PhotoFromData(photo, self);
		if (!stored) {
			return;
		}
		message.documentType = kDocumentTypePhoto;
		message.documentSerialized = serializeObject(*stored);
		StoreMediaLocation(photo->location(true), message);
	} else if (const auto document = media->document()) {
		const auto stored = DocumentFromData(document, self);
		if (!stored) {
			return;
		}
		message.documentType = kDocumentTypeDocument;
		message.documentSerialized = serializeObject(*stored);
		message.mimeType = document->mimeString().toStdString();
		StoreMediaLocation(document->location(true), message);
	}
}

bool hasStoredMedia(const AyuMessageBase &message) {
	return !message.documentSerialized.empty()
		&& (message.documentType == kDocumentTypePhoto
			|| message.documentType == kDocumentTypeDocument);
}

MTPMessageMedia mediaFromMessage(
		const AyuMessageBase &message,
		not_null<History*> history) {
	if (!hasStoredMedia(message)) {
		return MTP_messageMediaEmpty();
	}

	if (message.documentType == kDocumentTypePhoto) {
		const auto stored = ReadStoredMedia<MTPPhoto>(
			message.documentSerialized);
		if (!stored || stored->type() != mtpc_photo) {
			return MTP_messageMediaEmpty();
		}
		const auto photo = history->owner().processPhoto(*stored);
		RestoreMediaLocation(message, photo);
		return MTP_messageMediaPhoto(
			MTP_flags(MTPDmessageMediaPhoto::Flag::f_photo),
			*stored,
			MTPint(),
			MTPDocument());
	}

	const auto stored = ReadStoredMedia<MTPDocument>(
		message.documentSerialized);
	if (!stored || stored->type() != mtpc_document) {
		return MTP_messageMediaEmpty();
	}
	const auto document = history->owner().processDocument(*stored);
	RestoreMediaLocation(message, document);

	using Flag = MTPDmessageMediaDocument::Flag;
	auto flags = MTPDmessageMediaDocument::Flags();
	flags |= Flag::f_document;
	if (document->isVideoFile()) {
		flags |= Flag::f_video;
	}
	if (document->isVideoMessage()) {
		flags |= Flag::f_round;
	}
	if (document->isVoiceMessage()) {
		flags |= Flag::f_voice;
	}
	return MTP_messageMediaDocument(
		MTP_flags(flags),
		*stored,
		MTPVector<MTPDocument>(),
		MTPPhoto(),
		MTPint(),
		MTPint());
}

} // namespace AyuMapper

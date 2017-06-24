#pragma once
#include "catapult/ionet/PacketHandlers.h"
#include "catapult/model/ChainScore.h"
#include "catapult/model/RangeTypes.h"

namespace catapult { namespace io { class BlockStorageCache; } }

namespace catapult { namespace handlers {

	/// Prototype for a function that processes a range of blocks.
	using BlockRangeHandler = std::function<void (model::BlockRange&&)>;

	/// Registers a push block handler in \a handlers that validates a block and, if valid, forwards it to
	/// \a blockRangeHandler given a \a registry composed of known transactions.
	void RegisterPushBlockHandler(
			ionet::ServerPacketHandlers& handlers,
			const model::TransactionRegistry& registry,
			const BlockRangeHandler& blockRangeHandler);

	/// Registers a pull block handler in \a handlers that responds with a block in \a storage.
	void RegisterPullBlockHandler(ionet::ServerPacketHandlers& handlers, const io::BlockStorageCache& storage);

	/// Prototype for a function that returns a chain score.
	using ChainScoreSupplier = std::function<model::ChainScore ()>;

	/// Registers a chain info handler in \a handlers that responds with the height of the chain in \a storage
	/// and the score of the chain returned by \a chainScoreSupplier.
	void RegisterChainInfoHandler(
			ionet::ServerPacketHandlers& handlers,
			const io::BlockStorageCache& storage,
			const ChainScoreSupplier& chainScoreSupplier);

	/// Registers a block hashes handler in \a handlers that responds with at most \a maxHashes hashes in \a storage.
	void RegisterBlockHashesHandler(
			ionet::ServerPacketHandlers& handlers,
			const io::BlockStorageCache& storage,
			uint32_t maxHashes);

	/// Configuration for pull blocks handler.
	struct PullBlocksHandlerConfiguration {
		/// The minimum blocks to return.
		uint32_t MinBlocks;

		/// The maximum blocks to return.
		uint32_t MaxBlocks;

		/// The minimum response bytes.
		uint32_t MinResponseBytes;

		/// The maximum response bytes.
		uint32_t MaxResponseBytes;
	};

	/// Registers a pull blocks handler in \a handlers that responds with blocks from \a storage according to behavior
	/// specified in \a config.
	void RegisterPullBlocksHandler(
			ionet::ServerPacketHandlers& handlers,
			const io::BlockStorageCache& storage,
			const PullBlocksHandlerConfiguration& config);
}}
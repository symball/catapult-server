#include "src/observers/Observers.h"
#include "src/cache/AccountStateCache.h"
#include "src/observers/ImportanceCalculator.h"
#include "tests/test/cache/CacheTestUtils.h"
#include "tests/test/core/NotificationTestUtils.h"
#include "tests/test/nodeps/ParamsCapture.h"
#include "tests/test/plugins/ObserverTestUtils.h"
#include "tests/TestHarness.h"

namespace catapult { namespace observers {

#define TEST_CLASS RecalculateImportancesObserverTests

	DEFINE_COMMON_OBSERVER_TESTS(RecalculateImportances, CreateRestoreImportanceCalculator(), CreateRestoreImportanceCalculator())

	namespace {
		constexpr auto Importance_Grouping = 345u;

		struct ImportanceCalculatorParams {
		public:
			explicit ImportanceCalculatorParams(
					model::ImportanceHeight importanceHeight,
					const cache::AccountStateCacheDelta& cache)
					: ImportanceHeight(importanceHeight)
					, Cache(cache)
			{}

		public:
			const model::ImportanceHeight ImportanceHeight;
			const cache::AccountStateCacheDelta& Cache;
		};

		using ParamsVector = std::vector<ImportanceCalculatorParams>;

		class MockImportanceCalculator final
				: public ImportanceCalculator
				, public test::ParamsCapture<ImportanceCalculatorParams> {
		public:
			void recalculate(model::ImportanceHeight importanceHeight, cache::AccountStateCacheDelta& cache) const override {
				const_cast<MockImportanceCalculator*>(this)->push(importanceHeight, cache);
			}
		};

		class MockFailingImportanceCalculator final : public ImportanceCalculator {
		public:
			void recalculate(model::ImportanceHeight, cache::AccountStateCacheDelta&) const override {
				CATAPULT_THROW_RUNTIME_ERROR("unexpected call to MockFailingImportanceCalculator::recalculate");
			}
		};

		std::unique_ptr<MockImportanceCalculator> CreateCalculator() {
			return std::make_unique<MockImportanceCalculator>();
		}

		std::unique_ptr<ImportanceCalculator> CreateFailingCalculator() {
			return std::make_unique<MockFailingImportanceCalculator>();
		}

		struct CommitTraits {
			static NotifyMode Mode() { return NotifyMode::Commit; }
			static Height BaseHeight() { return Height(Importance_Grouping); }

			static auto CreateObserver(std::unique_ptr<ImportanceCalculator>&& pCalculator) {
				return CreateRecalculateImportancesObserver(std::move(pCalculator), CreateFailingCalculator());
			}
		};

		struct RollbackTraits {
			static NotifyMode Mode() { return NotifyMode::Rollback; }
			static Height BaseHeight() { return Height(Importance_Grouping + 1); }

			static auto CreateObserver(std::unique_ptr<ImportanceCalculator>&& pCalculator) {
				return CreateRecalculateImportancesObserver(CreateFailingCalculator(), std::move(pCalculator));
			}
		};

		template<typename TTraits>
		void AssertCalculation(NotifyMode mode, Height contextHeight, model::ImportanceHeight expectedImportanceHeight) {
			// Arrange:
			auto pCalculator = CreateCalculator();
			const auto& capturedParams = pCalculator->params();
			auto pObserver = TTraits::CreateObserver(std::move(pCalculator));

			auto config = model::BlockChainConfiguration::Uninitialized();
			config.ImportanceGrouping = Importance_Grouping;
			test::ObserverTestContext context(mode, contextHeight, config);

			auto notification = test::CreateBlockNotification();

			// Act:
			test::ObserveNotification(*pObserver, notification, context);

			// Assert:
			ASSERT_EQ(1u, capturedParams.size());
			EXPECT_EQ(expectedImportanceHeight, capturedParams[0].ImportanceHeight);
			EXPECT_EQ(&context.cache().sub<cache::AccountStateCache>(), &capturedParams[0].Cache);

			EXPECT_EQ(expectedImportanceHeight, context.state().LastRecalculationHeight);
		}
	}

	TEST(TEST_CLASS, RecalculateImportancesUsesCorrectHeightForModeCommit) {
		// Arrange:
		using Traits = CommitTraits;
		auto mode = NotifyMode::Commit;

		// Assert:
		AssertCalculation<Traits>(mode, Height(Importance_Grouping - 1), model::ImportanceHeight(1));
		AssertCalculation<Traits>(mode, Height(Importance_Grouping + 0), model::ImportanceHeight(Importance_Grouping));
		AssertCalculation<Traits>(mode, Height(Importance_Grouping + 1), model::ImportanceHeight(Importance_Grouping));

		AssertCalculation<Traits>(mode, Height(2 * Importance_Grouping - 1), model::ImportanceHeight(Importance_Grouping));
		AssertCalculation<Traits>(mode, Height(2 * Importance_Grouping + 0), model::ImportanceHeight(2 * Importance_Grouping));
		AssertCalculation<Traits>(mode, Height(2 * Importance_Grouping + 1), model::ImportanceHeight(2 * Importance_Grouping));
	}

	TEST(TEST_CLASS, RecalculateImportancesUsesCorrectHeightForModeRollback) {
		// Arrange:
		using Traits = RollbackTraits;
		auto mode = NotifyMode::Rollback;

		// Assert:
		AssertCalculation<Traits>(mode, Height(Importance_Grouping - 1), model::ImportanceHeight(1));
		AssertCalculation<Traits>(mode, Height(Importance_Grouping + 0), model::ImportanceHeight(1));
		AssertCalculation<Traits>(mode, Height(Importance_Grouping + 1), model::ImportanceHeight(Importance_Grouping));

		AssertCalculation<Traits>(mode, Height(2 * Importance_Grouping - 1), model::ImportanceHeight(Importance_Grouping));
		AssertCalculation<Traits>(mode, Height(2 * Importance_Grouping + 0), model::ImportanceHeight(Importance_Grouping));
		AssertCalculation<Traits>(mode, Height(2 * Importance_Grouping + 1), model::ImportanceHeight(2 * Importance_Grouping));
	}

	namespace {
		auto CreateEmptyCatapultCache() {
			auto config = model::BlockChainConfiguration::Uninitialized();
			config.ImportanceGrouping = Importance_Grouping;
			return test::CreateEmptyCatapultCache(config);
		}

		auto ConvertToImportanceHeight(Height height, NotifyMode mode) {
			return model::ConvertToImportanceHeight(
					height + (NotifyMode::Commit == mode ? Height(1) : Height(0)),
					Importance_Grouping);
		}

		template<typename TTraits>
		void AssertNoRecalculation(NotifyMode mode, Height height1, Height height2) {
			// Arrange:
			state::CatapultState state;
			auto cache = CreateEmptyCatapultCache();
			auto delta = cache.createDelta();
			ObserverState observerState(delta, state);

			auto pCalculator = CreateCalculator();
			const auto& capturedParams = pCalculator->params();
			auto pObserver = TTraits::CreateObserver(std::move(pCalculator));

			auto notification = test::CreateBlockNotification();

			// - trigger an initial calculation at height1
			test::ObserveNotification(*pObserver, notification, test::CreateObserverContext(observerState, height1, mode));

			// Act: trigger a recalculation at height2
			test::ObserveNotification(*pObserver, notification, test::CreateObserverContext(observerState, height2, mode));

			// Assert: only one calculation was performed
			ASSERT_EQ(1u, capturedParams.size());
			EXPECT_EQ(ConvertToImportanceHeight(height1, mode), capturedParams[0].ImportanceHeight);

			EXPECT_EQ(ConvertToImportanceHeight(height1, mode), state.LastRecalculationHeight);
		}

		template<typename TTraits>
		void AssertRecalculation(NotifyMode mode, Height height1, Height height2) {
			// Arrange:
			state::CatapultState state;
			auto cache = CreateEmptyCatapultCache();
			auto delta = cache.createDelta();
			ObserverState observerState(delta, state);

			auto pCalculator = CreateCalculator();
			const auto& capturedParams = pCalculator->params();
			auto pObserver = TTraits::CreateObserver(std::move(pCalculator));

			auto notification = test::CreateBlockNotification();

			// - trigger an initial calculation at height1
			test::ObserveNotification(*pObserver, notification, test::CreateObserverContext(observerState, height1, mode));

			// Act: trigger a recalculation at height2
			test::ObserveNotification(*pObserver, notification, test::CreateObserverContext(observerState, height2, mode));

			// Assert: two calculations were performed
			ASSERT_EQ(2u, capturedParams.size());
			EXPECT_EQ(ConvertToImportanceHeight(height1, mode), capturedParams[0].ImportanceHeight);
			EXPECT_EQ(ConvertToImportanceHeight(height2, mode), capturedParams[1].ImportanceHeight);

			EXPECT_EQ(ConvertToImportanceHeight(height2, mode), state.LastRecalculationHeight);
		}
	}

#define TRAITS_BASED_TEST(TEST_NAME) \
	template<typename TTraits> void TRAITS_TEST_NAME(TEST_CLASS, TEST_NAME)(); \
	TEST(TEST_CLASS, TEST_NAME##_Commit) { TRAITS_TEST_NAME(TEST_CLASS, TEST_NAME)<CommitTraits>(); } \
	TEST(TEST_CLASS, TEST_NAME##_Rollback) { TRAITS_TEST_NAME(TEST_CLASS, TEST_NAME)<RollbackTraits>(); } \
	template<typename TTraits> void TRAITS_TEST_NAME(TEST_CLASS, TEST_NAME)()

	TRAITS_BASED_TEST(RecalculationIsBypassedIfImportanceHeightEqualsLastCalculationHeight) {
		// Assert:
		auto mode = TTraits::Mode();
		auto baseHeight = TTraits::BaseHeight();
		for (auto i = 1u; i < 10; ++i)
			AssertNoRecalculation<TTraits>(mode, baseHeight, baseHeight + Height(i));

		AssertNoRecalculation<TTraits>(mode, baseHeight, baseHeight + Height(Importance_Grouping - 1));
	}

	TRAITS_BASED_TEST(RecalculationIsTriggeredIfImportanceHeightIsNotEqualToLastCalculationHeight) {
		// Assert:
		auto mode = TTraits::Mode();
		auto baseHeight = TTraits::BaseHeight();
		AssertRecalculation<TTraits>(mode, baseHeight, Height(1));
		AssertRecalculation<TTraits>(mode, baseHeight, baseHeight - Height(1));
		AssertRecalculation<TTraits>(mode, baseHeight, baseHeight + Height(Importance_Grouping));
		AssertRecalculation<TTraits>(mode, baseHeight, baseHeight + Height(Importance_Grouping + 1));
		AssertRecalculation<TTraits>(mode, baseHeight, baseHeight + Height(Importance_Grouping * 10));
	}
}}
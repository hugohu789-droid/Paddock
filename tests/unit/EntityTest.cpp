#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include <paddock/core/Components.hpp>
#include <paddock/core/Entity.hpp>

namespace paddock::core {
namespace {

TEST(WorldTest, EntityIdsAreUniqueAndAscending) {
  World world;

  const EntityId first = world.create();
  const EntityId second = world.create();

  EXPECT_NE(first, second);
  EXPECT_LT(value_of(first), value_of(second));
  EXPECT_NE(first, kInvalidEntity);
  EXPECT_EQ(world.entity_count(), 2U);
  EXPECT_TRUE(world.alive(first));
}

TEST(WorldTest, ComponentsAreAddedAndRetrievedByType) {
  World world;
  const EntityId ewe = world.create();

  world.add(ewe, Liveweight{62.0, 3.0});
  world.add(ewe, SpeciesRef{"sheep_romney"});

  const Liveweight* weight = world.get<Liveweight>(ewe);
  ASSERT_NE(weight, nullptr);
  EXPECT_DOUBLE_EQ(weight->liveweight_kg, 62.0);
  EXPECT_TRUE(world.has<SpeciesRef>(ewe));
  EXPECT_FALSE(world.has<Health>(ewe));
}

TEST(WorldTest, AddingToADeadEntityIsAnError) {
  World world;
  const EntityId ewe = world.create();
  ASSERT_TRUE(world.destroy(ewe));

  EXPECT_FALSE(world.alive(ewe));
  EXPECT_THROW(world.add(ewe, Liveweight{62.0, 3.0}), std::invalid_argument);
  EXPECT_FALSE(world.destroy(ewe));
}

TEST(WorldTest, DestroyingAnEntityDropsEveryComponent) {
  World world;
  const EntityId deer = world.create();
  const EntityId survivor = world.create();

  world.add(deer, Position{{1570000.0, 5180000.0}});
  world.add(deer, Health{1.0, 0.0});
  world.add(survivor, Position{{1570100.0, 5180000.0}});

  ASSERT_TRUE(world.destroy(deer));

  EXPECT_EQ(world.get<Position>(deer), nullptr);
  EXPECT_EQ(world.get<Health>(deer), nullptr);
  EXPECT_NE(world.get<Position>(survivor), nullptr);
  EXPECT_EQ(world.entity_count(), 1U);
}

// The same animal can be farmed stock in one run and a wild pest in the next.
// That is a difference in which components it carries, not in its type.
TEST(WorldTest, OwnershipIsAComponentNotAClass) {
  World world;
  const EntityId farmed_deer = world.create();
  const EntityId wild_deer = world.create();

  world.add(farmed_deer, SpeciesRef{"deer_red"});
  world.add(farmed_deer, Grazer{12.0, 0.4});
  world.add(farmed_deer, Owned{1});
  world.add(wild_deer, SpeciesRef{"deer_red"});
  world.add(wild_deer, Grazer{12.0, 0.4});

  EXPECT_TRUE(world.has<Owned>(farmed_deer));
  EXPECT_FALSE(world.has<Owned>(wild_deer));
  EXPECT_EQ(world.store<Grazer>().size(), 2U);
}

TEST(ComponentStoreTest, IterationIsSortedByEntityIdWhateverTheInsertionOrder) {
  ComponentStore<Liveweight> ascending;
  ComponentStore<Liveweight> descending;

  const std::vector<std::uint64_t> ids = {4, 1, 9, 2, 7};
  for (const std::uint64_t id : ids) {
    ascending.insert(EntityId{id}, Liveweight{static_cast<double>(id), 3.0});
  }
  for (auto it = ids.rbegin(); it != ids.rend(); ++it) {
    descending.insert(EntityId{*it}, Liveweight{static_cast<double>(*it), 3.0});
  }

  const std::vector<EntityId> expected = {EntityId{1}, EntityId{2}, EntityId{4}, EntityId{7},
                                          EntityId{9}};
  EXPECT_EQ(ascending.ids(), expected);
  EXPECT_EQ(descending.ids(), expected);

  std::vector<double> visited;
  ascending.for_each([&visited](EntityId /*id*/, const Liveweight& weight) {
    visited.push_back(weight.liveweight_kg);
  });
  EXPECT_EQ(visited, (std::vector<double>{1.0, 2.0, 4.0, 7.0, 9.0}));
}

TEST(ComponentStoreTest, InsertingTwiceReplacesTheComponent) {
  ComponentStore<Liveweight> store;

  store.insert(EntityId{3}, Liveweight{62.0, 3.0});
  store.insert(EntityId{3}, Liveweight{64.5, 3.5});

  EXPECT_EQ(store.size(), 1U);
  EXPECT_DOUBLE_EQ(store.at(EntityId{3}).liveweight_kg, 64.5);
}

TEST(ComponentStoreTest, ErasingKeepsTheRemainingComponentsAlignedWithTheirIds) {
  ComponentStore<Liveweight> store;
  for (std::uint64_t id = 1; id <= 5; ++id) {
    store.insert(EntityId{id}, Liveweight{static_cast<double>(id) * 10.0, 3.0});
  }

  EXPECT_TRUE(store.erase(EntityId{3}));
  EXPECT_FALSE(store.erase(EntityId{3}));
  EXPECT_EQ(store.size(), 4U);

  store.for_each([](EntityId id, const Liveweight& weight) {
    EXPECT_DOUBLE_EQ(weight.liveweight_kg, static_cast<double>(value_of(id)) * 10.0);
  });
  EXPECT_EQ(store.find(EntityId{3}), nullptr);
  EXPECT_THROW(static_cast<void>(store.at(EntityId{3})), std::out_of_range);
}

}  // namespace
}  // namespace paddock::core

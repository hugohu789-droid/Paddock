#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace paddock::core {

/// Stable identifier for a simulated thing - an animal, a paddock, a worker.
///
/// Identifiers are never reused within a run, and every random draw is keyed by
/// this value rather than by a container position, so no future change to
/// traversal order can change results.
enum class EntityId : std::uint64_t {};

inline constexpr EntityId kInvalidEntity{0};

[[nodiscard]] constexpr std::uint64_t value_of(EntityId id) noexcept {
  return static_cast<std::uint64_t>(id);
}

/// Type-erased handle so that World can drop an entity from every store.
class ComponentStoreBase {
 public:
  ComponentStoreBase() = default;
  ComponentStoreBase(const ComponentStoreBase&) = default;
  ComponentStoreBase& operator=(const ComponentStoreBase&) = default;
  ComponentStoreBase(ComponentStoreBase&&) = default;
  ComponentStoreBase& operator=(ComponentStoreBase&&) = default;
  virtual ~ComponentStoreBase() = default;

  virtual bool erase(EntityId id) = 0;
  [[nodiscard]] virtual std::size_t size() const noexcept = 0;
};

/// Dense storage for one component type, kept sorted by entity ID.
///
/// Sorted storage costs an insertion memmove and buys deterministic traversal:
/// `for_each` always visits entities in ascending ID order, whatever order they
/// were created or added in.
template <typename T>
class ComponentStore final : public ComponentStoreBase {
 public:
  using value_type = T;

  /// Adds or replaces the component for `id`.
  T& insert(EntityId id, T component) {
    const auto position = std::lower_bound(ids_.begin(), ids_.end(), id);
    const auto offset = static_cast<std::size_t>(position - ids_.begin());
    if (position != ids_.end() && *position == id) {
      values_[offset] = std::move(component);
      return values_[offset];
    }
    ids_.insert(position, id);
    values_.insert(values_.begin() + static_cast<std::ptrdiff_t>(offset), std::move(component));
    return values_[offset];
  }

  bool erase(EntityId id) override {
    const auto position = std::lower_bound(ids_.begin(), ids_.end(), id);
    if (position == ids_.end() || *position != id) {
      return false;
    }
    const auto offset = position - ids_.begin();
    ids_.erase(position);
    values_.erase(values_.begin() + offset);
    return true;
  }

  [[nodiscard]] bool contains(EntityId id) const noexcept { return find(id) != nullptr; }

  [[nodiscard]] T* find(EntityId id) noexcept {
    const auto offset = offset_of(id);
    return offset.has_value() ? &values_[*offset] : nullptr;
  }

  [[nodiscard]] const T* find(EntityId id) const noexcept {
    const auto offset = offset_of(id);
    return offset.has_value() ? &values_[*offset] : nullptr;
  }

  [[nodiscard]] T& at(EntityId id) {
    T* component = find(id);
    if (component == nullptr) {
      throw std::out_of_range("ComponentStore: entity " + std::to_string(value_of(id)) +
                              " has no such component");
    }
    return *component;
  }

  [[nodiscard]] std::size_t size() const noexcept override { return ids_.size(); }

  [[nodiscard]] bool empty() const noexcept { return ids_.empty(); }

  /// Entity IDs in ascending order.
  [[nodiscard]] const std::vector<EntityId>& ids() const noexcept { return ids_; }

  [[nodiscard]] const std::vector<T>& values() const noexcept { return values_; }

  template <typename Fn>
  void for_each(Fn&& fn) {
    for (std::size_t i = 0; i < ids_.size(); ++i) {
      fn(ids_[i], values_[i]);
    }
  }

  template <typename Fn>
  void for_each(Fn&& fn) const {
    for (std::size_t i = 0; i < ids_.size(); ++i) {
      fn(ids_[i], values_[i]);
    }
  }

 private:
  [[nodiscard]] std::optional<std::size_t> offset_of(EntityId id) const noexcept {
    const auto position = std::lower_bound(ids_.begin(), ids_.end(), id);
    if (position == ids_.end() || *position != id) {
      return std::nullopt;
    }
    return static_cast<std::size_t>(position - ids_.begin());
  }

  std::vector<EntityId> ids_;
  std::vector<T> values_;
};

/// Entities and their components.
///
/// There is no entity class hierarchy and there never will be: a deer is farmed
/// stock in one paddock and a wild pest in the next, which no inheritance tree
/// can express. Behaviour comes from which components an entity carries and
/// from the TOML definition it references.
class World {
 public:
  [[nodiscard]] EntityId create();

  /// Removes the entity and every component attached to it.
  bool destroy(EntityId id);

  [[nodiscard]] bool alive(EntityId id) const noexcept;

  [[nodiscard]] std::size_t entity_count() const noexcept { return alive_.size(); }

  /// Living entities in ascending ID order.
  [[nodiscard]] const std::vector<EntityId>& entities() const noexcept { return alive_; }

  template <typename T>
  [[nodiscard]] ComponentStore<T>& store() {
    const std::type_index key(typeid(T));
    auto found = stores_.find(key);
    if (found == stores_.end()) {
      found = stores_.emplace(key, std::make_unique<ComponentStore<T>>()).first;
    }
    return static_cast<ComponentStore<T>&>(*found->second);
  }

  /// Null when no component of this type has ever been added.
  template <typename T>
  [[nodiscard]] const ComponentStore<T>* find_store() const {
    const auto found = stores_.find(std::type_index(typeid(T)));
    if (found == stores_.end()) {
      return nullptr;
    }
    return static_cast<const ComponentStore<T>*>(found->second.get());
  }

  template <typename T>
  T& add(EntityId id, T component) {
    if (!alive(id)) {
      throw std::invalid_argument("World: entity " + std::to_string(value_of(id)) +
                                  " is not alive");
    }
    return store<T>().insert(id, std::move(component));
  }

  template <typename T>
  [[nodiscard]] T* get(EntityId id) {
    return store<T>().find(id);
  }

  template <typename T>
  [[nodiscard]] const T* get(EntityId id) const {
    const ComponentStore<T>* found = find_store<T>();
    return found == nullptr ? nullptr : found->find(id);
  }

  template <typename T>
  [[nodiscard]] bool has(EntityId id) const {
    return get<T>(id) != nullptr;
  }

  template <typename T>
  bool remove(EntityId id) {
    return store<T>().erase(id);
  }

 private:
  std::uint64_t next_id_ = 1;
  std::vector<EntityId> alive_;
  std::unordered_map<std::type_index, std::unique_ptr<ComponentStoreBase>> stores_;
};

}  // namespace paddock::core

#pragma once

namespace AquaCore {
namespace Events {

// Borrowed, transport-neutral emission boundary. The composition root owns the
// concrete sink and must keep it alive for every consumer holding a reference.
template <typename Event>
class EventSink {
public:
    // The caller owns event for this call. An adapter must copy data it needs
    // after return. Emission is a synchronous handoff, not a delivery guarantee.
    virtual void emit(const Event& event) = 0;

protected:
    // Consumers must not destroy the concrete sink through this interface.
    ~EventSink() = default;
};

// Explicit discard policy for compositions without an event consumer.
template <typename Event>
class NullEventSink final : public EventSink<Event> {
public:
    void emit(const Event&) override {
    }
};

} // namespace Events
} // namespace AquaCore

// Unit tests for the state/sequence model.
#include "sf_test.hpp"
#include "speculation_fabric/core/state.hpp"

using namespace speculation_fabric;

SF_TEST_FN(state_ref_validity) {
    StateRef empty{};
    SF_CHECK(!empty.is_valid());
    StateRef s{StateId{1}, StateGeneration{3}};
    SF_CHECK(s.is_valid());
    SF_CHECK_EQ(s.generation.get(), 3u);
}

SF_TEST_FN(reservation_active_state) {
    Reservation r;
    r.id = ReservationId{5};
    r.bytes = 1024;
    r.state = ReservationState::Reserved;
    r.purpose = StateOwner::Proposal;
    SF_CHECK(r.is_active());
    r.state = ReservationState::Released;
    SF_CHECK(!r.is_active());
}

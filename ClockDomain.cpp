#include "ClockDomain.hpp"

namespace ClockDomain {
using namespace std;
/// "Default" crosser with a 1:1 ratio
ClockDomainCrosser::ClockDomainCrosser(ClockUpdateCB *_callback)
    : callback(_callback)
    , clock1(1UL)
    , clock2(1UL)
    , counter1(0UL)
    , counter2(0UL)
{}

ClockDomainCrosser::ClockDomainCrosser(uint64_t _clock1,
                                       uint64_t _clock2,
                                       ClockUpdateCB *_callback)
    : callback(_callback)
    , clock1(_clock1)
    , clock2(_clock2)
    , counter1(0)
    , counter2(0)
{}

ClockDomainCrosser::ClockDomainCrosser(double ratio, ClockUpdateCB *_callback)
    : callback(_callback)
    , counter1(0)
    , counter2(0)
{
    // Compute numerator and denominator for ratio, then pass that to other constructor.
    double x = ratio;

    auto const MAX_ITER = 15;
    unsigned ns[MAX_ITER];
    unsigned ds[MAX_ITER] = {0, 1};
    double zs[MAX_ITER];
    zs[1] = x;
    ns[1] = static_cast<int>(x);

    size_t i;

    for (i = 1; i < MAX_ITER - 1; ++i) {
        if (fabs(x - double(ns[i]) / double(ds[i])) < 0.00005) {
            break;
        }
        /// @todo or, if the answers are the same as the last iteration, stop 
        zs[i + 1] = 1.0f / (zs[i] - static_cast<int>(floor(zs[i])));  // 1 / (fractional part of z_i)
        ds[i + 1] = ds[i] * static_cast<int>(floor(zs[i + 1])) + ds[i - 1];
        double const tmp = x*ds[i + 1];
        /// @todo fprem
        double const tmp2 = tmp - static_cast<int>(tmp);
        ns[i + 1] = tmp2 >= 0.5 ? ceil(tmp) : floor(tmp); // ghetto implementation of a rounding function
    }

    this->clock1 = ns[i];
    this->clock2 = ds[i];
}

void
ClockDomainCrosser::update(void)
{
    // short circuit case for 1:1 ratios
    if (this->clock1 == this->clock2 && this->callback) {
        (*callback)();
        return;
    }

    // Update counter 1.
    this->counter1 += this->clock1;

    while (this->counter2 < this->counter1) {
        this->counter2 += this->clock2;

        if (callback) {
            (*callback)();
        }
    }

    if (this->counter1 == this->counter2) {
        this->counter1 = 0;
        this->counter2 = 0;
    }
}

void
TestObj::cb(void)
{
    cout << "In Callback" << std::endl;
}

int
TestObj::test(void)
{
    auto const callback = new Callback<TestObj, void>(this, &TestObj::cb);

    ClockDomainCrosser x(0.5, callback);
    cout << "------------------------------------------\n";
    ClockDomainCrosser y(0.3333, callback);
    cout << "------------------------------------------\n";
    ClockDomainCrosser z(0.9, callback);
    cout << "------------------------------------------\n";

    for (auto i = 0u; i < 10u; ++i) {
        x.update();
        cout << "UPDATE: counter1= " << x.counter1 << "; counter2= " << x.counter2 << "; " << endl;
    }

    return 0;
}

}  // namespace ClockDomain

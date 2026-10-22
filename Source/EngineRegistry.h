#pragma once
#include "engines/TestSineEngine.h"
#include <memory>

namespace ps
{
// The only place a new engine is registered. IDs are permanent: they are saved in plugin state.
namespace engines
{
    struct Entry
    {
        const char* id;
        const char* name;
        std::unique_ptr<Engine> (*create)();
    };

    inline const std::vector<Entry>& all()
    {
        static const std::vector<Entry> list {
            { "test.sine", "Test Sine", [] { return std::unique_ptr<Engine> (std::make_unique<TestSineEngine>()); } },
        };
        return list;
    }

    // Returns nullptr for an empty or unknown ID (the layer stays empty).
    inline std::unique_ptr<Engine> create (const juce::String& id)
    {
        for (auto& e : all())
            if (id == e.id)
                return e.create();
        return nullptr;
    }
}
}

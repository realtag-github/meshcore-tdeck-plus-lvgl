#include <deque>
#include <iostream>
#include <set>

#include "app/navigation.h"

namespace {

bool reaches_home(meshcore::ScreenId start) {
    std::deque<meshcore::ScreenId> pending;
    std::set<meshcore::ScreenId> visited;
    pending.push_back(start);

    while (!pending.empty()) {
        const auto screen = pending.front();
        pending.pop_front();
        if (screen == meshcore::ScreenId::Home) {
            return true;
        }
        if (!visited.insert(screen).second) {
            continue;
        }
        if (screen != meshcore::ScreenId::Home) {
            pending.push_back(meshcore::ScreenId::Home);
        }
        for (const auto& action : meshcore::screen_actions(screen)) {
            pending.push_back(action.target);
        }
    }
    return false;
}

}  // namespace

int main() {
    bool ok = true;
    for (const auto screen : meshcore::all_screens()) {
        bool has_exit = screen != meshcore::ScreenId::Home;
        for (const auto& action : meshcore::screen_actions(screen)) {
            if (action.label.empty()) {
                std::cerr << meshcore::screen_title(screen) << " has an empty touch action label\n";
                ok = false;
            }
            if (!meshcore::is_known_screen(action.target)) {
                std::cerr << meshcore::screen_title(screen) << " targets an unknown screen\n";
                ok = false;
            }
            if (action.target != screen) {
                has_exit = true;
            }
        }

        if (!has_exit) {
            std::cerr << meshcore::screen_title(screen) << " cannot be exited by touch\n";
            ok = false;
        }

        if (screen != meshcore::ScreenId::Home && !reaches_home(screen)) {
            std::cerr << meshcore::screen_title(screen) << " has no touch path back to Home\n";
            ok = false;
        }
    }

    if (!ok) {
        return 1;
    }

    std::cout << "Navigation touch-exit test passed for "
              << meshcore::all_screens().size() << " screens\n";
    return 0;
}

#include "core/SnapshotHistory.h"

#include <iostream>

int main()
{
#define CHECK(expression)                                                        \
    do                                                                           \
    {                                                                            \
        if(!(expression))                                                        \
        {                                                                        \
            std::cerr << "Check failed at line " << __LINE__                    \
                      << ": " #expression << '\n';                              \
            return 1;                                                            \
        }                                                                        \
    } while(false)

    lighttrack::SnapshotHistory<int> history(2);
    CHECK(history.Commit(1));
    CHECK(history.Current() == 1);
    CHECK(!history.Commit(1));

    CHECK(history.Commit(2));
    CHECK(history.Commit(3));
    CHECK(history.Commit(4));
    CHECK(history.Current() == 4);
    CHECK(history.UndoTarget() != nullptr);
    CHECK(*history.UndoTarget() == 3);

    history.AcceptUndo();
    CHECK(history.Current() == 3);
    CHECK(*history.UndoTarget() == 2);
    CHECK(*history.RedoTarget() == 4);

    history.AcceptUndo();
    CHECK(history.Current() == 2);
    CHECK(!history.CanUndo());

    history.AcceptRedo();
    CHECK(history.Current() == 3);
    history.ReplaceCurrent(30);
    CHECK(history.Current() == 30);
    CHECK(*history.RedoTarget() == 4);
    CHECK(history.Commit(31));
    CHECK(!history.CanRedo());

    history.Clear();
    CHECK(!history.IsInitialized());

#undef CHECK
    return 0;
}

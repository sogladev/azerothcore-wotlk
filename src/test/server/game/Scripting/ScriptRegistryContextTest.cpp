/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ScriptMgr.h"
#include "ScriptDefines/CreatureScript.h"
#include "ScriptDefines/MiscScript.h"
#include "ScriptDefines/ModuleScript.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <optional>

namespace
{
    std::string const TestContextA = "context-a";
    std::string const TestContextB = "context-b";

    /// A code-only script that enables one hook, so a test can tell whether a
    /// release also took the script out of the dispatch of that hook.
    class ContextTestMiscScript : public MiscScript
    {
    public:
        explicit ContextTestMiscScript(char const* name)
            : MiscScript(name, { MISCHOOK_ON_DESTRUCT_GROUP }) { }

        ~ContextTestMiscScript() override { ++DestroyedCount; }

        inline static uint32 DestroyedCount = 0;
    };

    /// A database-bound script: no test process has a database that names it, so
    /// it waits in the after-load list of its registry.
    class ContextTestCreatureScript : public CreatureScript
    {
    public:
        explicit ContextTestCreatureScript(char const* name) : CreatureScript(name) { }

        ~ContextTestCreatureScript() override { ++DestroyedCount; }

        inline static uint32 DestroyedCount = 0;
    };

    class ContextTestModuleScript : public ModuleScript
    {
    public:
        explicit ContextTestModuleScript(char const* name) : ModuleScript(name) { }

        ~ContextTestModuleScript() override { ++DestroyedCount; }

        inline static uint32 DestroyedCount = 0;
    };

    /// The id the given script is registered under, or nothing once it left the
    /// registry. The script type is given explicitly: deducing it from the pointer
    /// would name the concrete test script and instantiate its own registry.
    template<typename ScriptType>
    std::optional<uint32> FindScriptId(ScriptType const* script)
    {
        for (auto const& [id, registered] : ScriptRegistry<ScriptType>::ScriptPointerList)
            if (registered == script)
                return id;

        return std::nullopt;
    }

    bool IsInHookBucket(std::vector<MiscScript*> const& hookScripts, MiscScript const* script)
    {
        return std::find(hookScripts.begin(), hookScripts.end(), script) != hookScripts.end();
    }
}

// A script is filed under the static context when nothing selected a context: the
// scripts of the statically linked set are never orphaned by the first swap.
TEST(ScriptRegistryContextTest, ScriptsRegisteredWithoutAContextBelongToTheStaticContext)
{
    // ModuleScript stands in for the static set because a script of the static
    // context can only leave its registry through the type's own Unload(), and
    // that must not delete what other test files registered. No other test
    // registers a ModuleScript.
    auto* script = new ContextTestModuleScript("ContextTestStaticScript");

    std::optional<uint32> const id = FindScriptId<ModuleScript>(script);
    ASSERT_TRUE(id);

    EXPECT_EQ(sScriptRegistryCompositum->GetScriptContextOfScriptName(script->GetName()),
              ScriptMgr::GetNameOfStaticContext());

    bool filedInStaticContext = false;
    auto const bounds = ScriptRegistry<ModuleScript>::ContextScriptIds.equal_range(ScriptMgr::GetNameOfStaticContext());
    for (auto itr = bounds.first; itr != bounds.second; ++itr)
        filedInStaticContext = filedInStaticContext || itr->second == *id;
    EXPECT_TRUE(filedInStaticContext);

    ScriptRegistry<ModuleScript>::Unload();
    EXPECT_EQ(ContextTestModuleScript::DestroyedCount, 1u);
}

// A module's own hook class dies with the library that registered it: nothing else
// owns it, so leaving it behind would let a hook dispatch into unmapped code.
TEST(ScriptRegistryContextTest, ReleaseRemovesTheModuleScriptsOfItsContext)
{
    ContextTestModuleScript::DestroyedCount = 0;

    sScriptMgr->SetScriptContext(TestContextB);
    auto* script = new ContextTestModuleScript("ContextTestReleasedModuleScript");
    sScriptMgr->SetScriptContext(ScriptMgr::GetNameOfStaticContext());

    std::optional<uint32> const id = FindScriptId<ModuleScript>(script);
    ASSERT_TRUE(id);
    ASSERT_EQ(ScriptRegistry<ModuleScript>::GetScriptById(*id), script);

    sScriptMgr->ReleaseScriptContext(TestContextB);
    EXPECT_FALSE(ScriptRegistry<ModuleScript>::GetScriptById(*id));
    EXPECT_TRUE(sScriptRegistryCompositum->GetScriptContextOfScriptName(script->GetName()).empty());
    EXPECT_EQ(ContextTestModuleScript::DestroyedCount, 0u);

    sScriptMgr->SwapScriptContext();
    EXPECT_EQ(ContextTestModuleScript::DestroyedCount, 1u);
}

// Releasing one library must leave everything the other libraries registered
// alone, in the registry list and in the hook dispatch alike.
TEST(ScriptRegistryContextTest, ReleaseRemovesOnlyTheScriptsOfItsContext)
{
    ContextTestMiscScript::DestroyedCount = 0;

    auto* staticScript = new ContextTestMiscScript("ContextTestStaticSurvivor");
    sScriptMgr->SetScriptContext(TestContextA);
    auto* releasedScript = new ContextTestMiscScript("ContextTestReleased");
    sScriptMgr->SetScriptContext(ScriptMgr::GetNameOfStaticContext());

    std::optional<uint32> const staticId = FindScriptId<MiscScript>(staticScript);
    std::optional<uint32> const releasedId = FindScriptId<MiscScript>(releasedScript);
    ASSERT_TRUE(staticId);
    ASSERT_TRUE(releasedId);

    std::vector<MiscScript*> const& hookScripts = ScriptRegistry<MiscScript>::EnabledHooks[MISCHOOK_ON_DESTRUCT_GROUP];
    ASSERT_TRUE(IsInHookBucket(hookScripts, staticScript));
    ASSERT_TRUE(IsInHookBucket(hookScripts, releasedScript));

    sScriptMgr->ReleaseScriptContext(TestContextA);

    EXPECT_EQ(ScriptRegistry<MiscScript>::GetScriptById(*staticId), staticScript);
    EXPECT_FALSE(ScriptRegistry<MiscScript>::GetScriptById(*releasedId));
    EXPECT_TRUE(IsInHookBucket(hookScripts, staticScript));
    EXPECT_FALSE(IsInHookBucket(hookScripts, releasedScript));

    sScriptMgr->SwapScriptContext();
    EXPECT_EQ(ContextTestMiscScript::DestroyedCount, 1u);

    ScriptRegistry<MiscScript>::Unload();
}

// A released script is not deleted on the spot: a hook could still reach it until
// the change set is committed.
TEST(ScriptRegistryContextTest, ReleasedScriptsAreDestroyedAtTheCommit)
{
    ContextTestMiscScript::DestroyedCount = 0;

    sScriptMgr->SetScriptContext(TestContextA);
    new ContextTestMiscScript("ContextTestDeferredDelete");
    sScriptMgr->SetScriptContext(ScriptMgr::GetNameOfStaticContext());

    sScriptMgr->ReleaseScriptContext(TestContextA);
    EXPECT_EQ(ContextTestMiscScript::DestroyedCount, 0u);

    sScriptMgr->SwapScriptContext();
    EXPECT_EQ(ContextTestMiscScript::DestroyedCount, 1u);
}

// The script counter must not keep counting the scripts of a library that is no
// longer loaded.
TEST(ScriptRegistryContextTest, ScriptCountReturnsToItsStartingValueAfterACommit)
{
    uint32 const scriptCountBefore = sScriptMgr->GetScriptCount();

    sScriptMgr->SetScriptContext(TestContextA);
    new ContextTestMiscScript("ContextTestCounted");
    sScriptMgr->SetScriptContext(ScriptMgr::GetNameOfStaticContext());
    ASSERT_EQ(sScriptMgr->GetScriptCount(), scriptCountBefore + 1);

    sScriptMgr->ReleaseScriptContext(TestContextA);

    // Still alive, so still counted.
    EXPECT_EQ(sScriptMgr->GetScriptCount(), scriptCountBefore + 1);

    sScriptMgr->SwapScriptContext();
    EXPECT_EQ(sScriptMgr->GetScriptCount(), scriptCountBefore);
}

// A script whose name the database never learned waits in the after-load list, and
// the list owns it until it is promoted: releasing its context has to take it out
// of there, or the release would leave a dangling entry behind.
TEST(ScriptRegistryContextTest, ReleaseRemovesTheDeferredScriptsOfItsContext)
{
    ContextTestCreatureScript::DestroyedCount = 0;

    sScriptMgr->SetScriptContext(TestContextB);
    new ContextTestCreatureScript("ContextTestDeferredCreature");
    sScriptMgr->SetScriptContext(ScriptMgr::GetNameOfStaticContext());

    ASSERT_EQ(ScriptRegistry<CreatureScript>::ALScripts.size(), 1u);

    sScriptMgr->ReleaseScriptContext(TestContextB);
    EXPECT_TRUE(ScriptRegistry<CreatureScript>::ALScripts.empty());
    EXPECT_EQ(ContextTestCreatureScript::DestroyedCount, 0u);

    sScriptMgr->SwapScriptContext();
    EXPECT_EQ(ContextTestCreatureScript::DestroyedCount, 1u);
}

// Releasing a context nothing registered is a no-op: neither the registry list nor
// the hook dispatch may change.
TEST(ScriptRegistryContextTest, ReleasingAnUnknownContextChangesNothing)
{
    ContextTestMiscScript::DestroyedCount = 0;

    auto* script = new ContextTestMiscScript("ContextTestUnknownContext");
    std::optional<uint32> const id = FindScriptId<MiscScript>(script);
    ASSERT_TRUE(id);

    sScriptMgr->ReleaseScriptContext("context-no-script-uses");
    sScriptMgr->SwapScriptContext();

    EXPECT_EQ(ContextTestMiscScript::DestroyedCount, 0u);
    EXPECT_EQ(ScriptRegistry<MiscScript>::GetScriptById(*id), script);
    EXPECT_TRUE(IsInHookBucket(ScriptRegistry<MiscScript>::EnabledHooks[MISCHOOK_ON_DESTRUCT_GROUP], script));

    ScriptRegistry<MiscScript>::Unload();
}

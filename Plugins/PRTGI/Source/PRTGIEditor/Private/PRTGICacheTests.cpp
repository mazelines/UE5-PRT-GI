#include "PRTGI.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/AutomationCommon.h"

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FPRTGICheckPIECache, FAutomationTestBase*, Test);
bool FPRTGICheckPIECache::Update()
{
    PRTGI::RunCacheIdentityChecks(*Test);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPRTGIPIECacheIdentityTest,
    "PRTGI.Integration.EditorPIECacheIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPRTGIPIECacheIdentityTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(3.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FPRTGICheckPIECache(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif

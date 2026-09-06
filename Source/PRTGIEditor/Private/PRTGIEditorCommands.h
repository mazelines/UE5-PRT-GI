#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

/** PRT GI 在 Level Editor 中使用的全局快捷键。 */
class FPRTGIEditorCommands final : public TCommands<FPRTGIEditorCommands>
{
public:
    FPRTGIEditorCommands();

    virtual void RegisterCommands() override;

    TSharedPtr<FUICommandInfo> SwitchOffline;
    TSharedPtr<FUICommandInfo> SwitchPRT;
    TSharedPtr<FUICommandInfo> SwitchLumen;
    TSharedPtr<FUICommandInfo> ToggleIndirectOnly;
    TSharedPtr<FUICommandInfo> ToggleProbes;
    TSharedPtr<FUICommandInfo> CycleBounces;
    TSharedPtr<FUICommandInfo> RotateYawNegative;
    TSharedPtr<FUICommandInfo> RotateYawPositive;
    TSharedPtr<FUICommandInfo> RotatePitchNegative;
    TSharedPtr<FUICommandInfo> RotatePitchPositive;
    TSharedPtr<FUICommandInfo> Bake;
};

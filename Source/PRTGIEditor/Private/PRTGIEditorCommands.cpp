#include "PRTGIEditorCommands.h"

#include "InputCoreTypes.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "PRTGIEditorCommands"

FPRTGIEditorCommands::FPRTGIEditorCommands()
    : TCommands<FPRTGIEditorCommands>(
        TEXT("PRTGIEditor"),
        LOCTEXT("ContextName", "PRT GI"),
        NAME_None,
        FAppStyle::GetAppStyleSetName())
{
}

void FPRTGIEditorCommands::RegisterCommands()
{
    UI_COMMAND(CycleBounces,"PRT Bounce Count","Cycle through 1-4 indirect bounces. Requires a precomputed multi-bounce cache.",
        EUserInterfaceActionType::Button,FInputChord(EModifierKey::Control | EModifierKey::Alt,EKeys::G));
    UI_COMMAND(
        ToggleProbes,
        "Show PRT Probes",
        "Show or hide probes. Each volume's Display Mode controls position markers or lighting spheres.",
        EUserInterfaceActionType::Button,
        FInputChord(EModifierKey::Control | EModifierKey::Alt, EKeys::P));

    UI_COMMAND(
        ToggleIndirectOnly,
        "PRT Indirect Only",
        "Show only PRT indirect diffuse lighting. Toggle again to restore the full view.",
        EUserInterfaceActionType::Button,
        FInputChord(EModifierKey::Control | EModifierKey::Alt, EKeys::Four));

    UI_COMMAND(
        SwitchOffline,
        "Disable Dynamic GI",
        "Disable dynamic GI. Existing baked lighting remains; this does not build offline lighting.",
        EUserInterfaceActionType::Button,
        FInputChord(EModifierKey::Control | EModifierKey::Alt, EKeys::One));

    UI_COMMAND(
        SwitchPRT,
        "PRT GI",
        "Switch to PRT GI.",
        EUserInterfaceActionType::Button,
        FInputChord(EModifierKey::Control | EModifierKey::Alt, EKeys::Two));

    UI_COMMAND(
        SwitchLumen,
        "Lumen GI",
        "Switch to Lumen GI.",
        EUserInterfaceActionType::Button,
        FInputChord(EModifierKey::Control | EModifierKey::Alt, EKeys::Three));

    UI_COMMAND(
        RotateYawNegative,
        "Directional Light Yaw -",
        "Decrease the primary DirectionalLight yaw by one configured step.",
        EUserInterfaceActionType::Button,
        FInputChord(EModifierKey::Control | EModifierKey::Alt, EKeys::LeftBracket));

    UI_COMMAND(
        RotateYawPositive,
        "Directional Light Yaw +",
        "Increase the primary DirectionalLight yaw by one configured step.",
        EUserInterfaceActionType::Button,
        FInputChord(EModifierKey::Control | EModifierKey::Alt, EKeys::RightBracket));

    UI_COMMAND(
        RotatePitchNegative,
        "Directional Light Pitch -",
        "Decrease the primary DirectionalLight pitch by one configured step.",
        EUserInterfaceActionType::Button,
        FInputChord(EModifierKey::Control | EModifierKey::Alt | EModifierKey::Shift, EKeys::LeftBracket));

    UI_COMMAND(
        RotatePitchPositive,
        "Directional Light Pitch +",
        "Increase the primary DirectionalLight pitch by one configured step.",
        EUserInterfaceActionType::Button,
        FInputChord(EModifierKey::Control | EModifierKey::Alt | EModifierKey::Shift, EKeys::RightBracket));

    UI_COMMAND(
        Bake,
        "Bake PRT GI",
        "Build legacy global directional PRT transport from the current scene. Does not build Actor Volumes.",
        EUserInterfaceActionType::Button,
        FInputChord(EModifierKey::Control | EModifierKey::Alt, EKeys::B));
}

#undef LOCTEXT_NAMESPACE

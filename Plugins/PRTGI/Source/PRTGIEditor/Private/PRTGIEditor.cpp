#include "Modules/ModuleManager.h"

#include "Framework/Commands/UICommandList.h"
#include "LevelEditor.h"
#include "PRTGI.h"
#include "PRTGIEditorCommands.h"
#include "PRTGISettings.h"
#include "PRTProbeVolume.h"
#include "IDetailCustomization.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "PropertyHandle.h"
#include "PropertyEditorModule.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

class FPRTProbeVolumeDetails final : public IDetailCustomization
{
public:
    static TSharedRef<IDetailCustomization> MakeInstance() { return MakeShared<FPRTProbeVolumeDetails>(); }

    virtual void CustomizeDetails(IDetailLayoutBuilder& Builder) override
    {
        TArray<TWeakObjectPtr<UObject>> Objects;
        Builder.GetObjectsBeingCustomized(Objects);
        const TWeakObjectPtr<APRTProbeVolume> Volume = Objects.Num() == 1
            ? Cast<APRTProbeVolume>(Objects[0].Get()) : nullptr;
        // Category paths are metadata, not nested categories for EditCategory or
        // CallInEditor buttons. Own the layout once so every row has one home.
        IDetailCategoryBuilder& Category = Builder.EditCategory(TEXT("PRT"));
        TMap<FString, IDetailGroup*> Groups;
        for (const TCHAR* Name : {TEXT("Volume"), TEXT("Build"), TEXT("Blending"),
            TEXT("Display"), TEXT("Status"), TEXT("Identity"), TEXT("Legacy Import")})
        {
            const FString GroupName(Name);
            Groups.Add(GroupName, &Category.AddGroup(FName(Name), FText::FromString(GroupName),
                GroupName == TEXT("Identity") || GroupName == TEXT("Legacy Import")));
            // Suppress the literal top-level categories created by auto buttons
            // or older custom rows, while keeping their reflected API metadata.
            Builder.HideCategory(FName(*(TEXT("PRT|") + GroupName)));
        }
        for (TFieldIterator<FProperty> It(APRTProbeVolume::StaticClass()); It; ++It)
        {
            const FString Path = It->GetMetaData(TEXT("Category"));
            if (!Path.StartsWith(TEXT("PRT|"))) continue;
            IDetailGroup** Group = Groups.Find(Path.Mid(4));
            if (!Group) continue;
            const TSharedRef<IPropertyHandle> Property = Builder.GetProperty(It->GetFName());
            if (!Property->IsValidHandle()) continue;
            Builder.HideProperty(Property);
            // A single selection gets the existing live progress widget; multi-
            // selection keeps the ordinary multi-value progress/stage fields.
            if (Volume.IsValid() && (It->GetFName() == GET_MEMBER_NAME_CHECKED(APRTProbeVolume, BuildProgress)
                || It->GetFName() == GET_MEMBER_NAME_CHECKED(APRTProbeVolume, BuildStage))) continue;
            (*Group)->AddPropertyRow(Property);
        }
        Objects.RemoveAll([](const TWeakObjectPtr<UObject>& Object)
            { return !Object.IsValid() || Object->HasAnyFlags(RF_ArchetypeObject); });
        if (!Objects.IsEmpty())
        {
            Groups[TEXT("Build")]->AddWidgetRow()
            .FilterString(FText::FromString(TEXT("Build Precomputation Cancel Build")))
            .WholeRowContent()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [ SNew(SButton).Text(FText::FromString(TEXT("Build Precomputation")))
                    .OnClicked_Lambda([Objects]()
                    {
                        for (const auto& Object : Objects)
                            if (auto* Item = Cast<APRTProbeVolume>(Object.Get())) Item->BuildPrecomputation();
                        return FReply::Handled();
                    }) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f)
                [ SNew(SButton).Text(FText::FromString(TEXT("Cancel Build")))
                    .OnClicked_Lambda([Objects]()
                    {
                        for (const auto& Object : Objects)
                            if (auto* Item = Cast<APRTProbeVolume>(Object.Get())) Item->CancelBuild();
                        return FReply::Handled();
                    }) ]
            ];
        }
        if (!Volume.IsValid()) return;
        Groups[TEXT("Build")]->AddWidgetRow()
        .FilterString(FText::FromString(TEXT("Build Progress Build Stage")))
        .NameContent()
        [ SNew(STextBlock).Text(FText::FromString(TEXT("Build Progress"))).Font(IDetailLayoutBuilder::GetDetailFont()) ]
        .ValueContent().MinDesiredWidth(240.f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBox).HeightOverride(20.f)
                .ToolTipText(FText::FromString(TEXT("Progress is weighted by build stage, not an estimate of remaining time. Reaches 100% only after saving succeeds.")))
                [
                    SNew(SOverlay)
                    + SOverlay::Slot()
                    [ SNew(SProgressBar).Percent_Lambda([Volume]() -> TOptional<float>
                        { return Volume.IsValid() ? FMath::Clamp(Volume->BuildProgress, 0.f, 1.f) : 0.f; }) ]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [ SNew(STextBlock).ColorAndOpacity(FLinearColor::White)
                        .ShadowOffset(FVector2D(1.f, 1.f)).ShadowColorAndOpacity(FLinearColor::Black)
                        .Text_Lambda([Volume]()
                        {
                            const float Progress = Volume.IsValid() ? FMath::Clamp(Volume->BuildProgress, 0.f, 1.f) : 0.f;
                            return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::FloorToInt(Progress * 100.f)));
                        }) ]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
            [ SNew(STextBlock).AutoWrapText(true).Font(IDetailLayoutBuilder::GetDetailFont())
                .Text_Lambda([Volume]() { return FText::FromString(Volume.IsValid() ? Volume->BuildStage : FString()); }) ]
        ];
    }
};

class FPRTGIEditorModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"))
            .RegisterCustomClassLayout(TEXT("PRTProbeVolume"), FOnGetDetailCustomizationInstance::CreateStatic(&FPRTProbeVolumeDetails::MakeInstance));
        FPRTGIEditorCommands::Register();

        FLevelEditorModule& LevelEditorModule =
            FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
        const TSharedRef<FUICommandList> GlobalActions = LevelEditorModule.GetGlobalLevelEditorActions();
        LevelEditorActions = GlobalActions;

        const FPRTGIEditorCommands& Commands = FPRTGIEditorCommands::Get();
        GlobalActions->MapAction(Commands.CycleBounces,FExecuteAction::CreateStatic(&PRTGI::CycleBounces));
        GlobalActions->MapAction(
            Commands.ToggleProbes,
            FExecuteAction::CreateStatic(&PRTGI::ToggleProbes));
        GlobalActions->MapAction(
            Commands.ToggleIndirectOnly,
            FExecuteAction::CreateStatic(&PRTGI::ToggleIndirectOnly));
        GlobalActions->MapAction(
            Commands.SwitchOffline,
            FExecuteAction::CreateLambda([] { PRTGI::SetMode(PRTGI::EMode::Offline); }));
        GlobalActions->MapAction(
            Commands.SwitchPRT,
            FExecuteAction::CreateLambda([] { PRTGI::SetMode(PRTGI::EMode::PRT); }));
        GlobalActions->MapAction(
            Commands.SwitchLumen,
            FExecuteAction::CreateLambda([] { PRTGI::SetMode(PRTGI::EMode::Lumen); }));

        GlobalActions->MapAction(
            Commands.RotateYawNegative,
            FExecuteAction::CreateLambda([] { RotateLight(-GetRotationStep(), 0.0f); }));
        GlobalActions->MapAction(
            Commands.RotateYawPositive,
            FExecuteAction::CreateLambda([] { RotateLight(GetRotationStep(), 0.0f); }));
        GlobalActions->MapAction(
            Commands.RotatePitchNegative,
            FExecuteAction::CreateLambda([] { RotateLight(0.0f, -GetRotationStep()); }));
        GlobalActions->MapAction(
            Commands.RotatePitchPositive,
            FExecuteAction::CreateLambda([] { RotateLight(0.0f, GetRotationStep()); }));
        GlobalActions->MapAction(
            Commands.Bake,
            FExecuteAction::CreateLambda([] { PRTGI::RequestBake(); }));
    }

    virtual void ShutdownModule() override
    {
        if (FPropertyEditorModule* PropertyEditor = FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
            PropertyEditor->UnregisterCustomClassLayout(TEXT("PRTProbeVolume"));
        // Live Coding 或插件重载时先从共享命令表解绑，再注销命令上下文。
        if (const TSharedPtr<FUICommandList> GlobalActions = LevelEditorActions.Pin())
        {
            const FPRTGIEditorCommands& Commands = FPRTGIEditorCommands::Get();
            GlobalActions->UnmapAction(Commands.CycleBounces);
            GlobalActions->UnmapAction(Commands.ToggleProbes);
            GlobalActions->UnmapAction(Commands.ToggleIndirectOnly);
            GlobalActions->UnmapAction(Commands.SwitchOffline);
            GlobalActions->UnmapAction(Commands.SwitchPRT);
            GlobalActions->UnmapAction(Commands.SwitchLumen);
            GlobalActions->UnmapAction(Commands.RotateYawNegative);
            GlobalActions->UnmapAction(Commands.RotateYawPositive);
            GlobalActions->UnmapAction(Commands.RotatePitchNegative);
            GlobalActions->UnmapAction(Commands.RotatePitchPositive);
            GlobalActions->UnmapAction(Commands.Bake);
        }

        LevelEditorActions.Reset();
        FPRTGIEditorCommands::Unregister();
    }

private:
    static float GetRotationStep()
    {
        return GetDefault<UPRTGISettings>()->RotationStepDegrees;
    }

    static void RotateLight(const float YawDeltaDegrees, const float PitchDeltaDegrees)
    {
        PRTGI::RotatePrimaryDirectionalLight(YawDeltaDegrees, PitchDeltaDegrees);
    }

    TWeakPtr<FUICommandList> LevelEditorActions;
};

IMPLEMENT_MODULE(FPRTGIEditorModule, PRTGIEditor)

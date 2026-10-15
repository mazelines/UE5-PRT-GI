# 빠른 시작과 Lumen 비교

1. 콘텐츠 브라우저에서 **Show Plugin Content**를 켭니다.
2. **PRTGI Content → Maps → PRTGI_Showcase**를 엽니다. 에셋 경로는 `/PRTGI/Maps/PRTGI_Showcase`입니다.
3. 저장소에는 사전 계산 캐시가 없으므로 각 `PRTProbeVolume`을 선택해 **Build Precomputation**을 실행하고 완료 상태를 확인합니다.
4. 에디터 뷰포트나 PIE 게임 화면을 클릭한 뒤 **Ctrl+Alt+2**로 PRT를 켜고, **Ctrl+Alt+3**으로 Lumen과 비교합니다.
5. **Ctrl+Alt+4**로 PRT 간접광만 확인하고, **Ctrl+Alt+P**로 프로브를 표시합니다.
6. **Ctrl+Alt+[ / ]**로 태양을 회전해 조명 변화를 확인합니다.

기존 플러그인은 UE 5.8 Renderer 인터페이스를 기준으로 작성됐습니다. 하드웨어 레이 트레이싱 Lumen과 비교하려면 프로젝트에 DX12 / SM6, Ray Tracing 및 관련 지원 설정이 필요합니다. 플러그인은 프로젝트 렌더링 설정을 강제로 덮어쓰지 않습니다.

지원 GPU에서 **Support Hardware Ray Tracing**, **Support Compute Skin Cache**, **Use Hardware Ray Tracing when available**을 활성화하세요. 재시작이 필요한 설정을 변경했다면 에디터를 다시 시작합니다. 관련 설명은 Epic의 [Lumen Technical Details](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine)를 참고하세요.

포함된 예제 맵의 기본 프로필은 일시적으로 `r.Lumen.HardwareRayTracing=1`, `r.Lumen.ScreenProbeGather.ScreenTraces=0`, `r.EyeAdaptation.CachedLightingPreExposure=8`을 적용하며, 맵을 벗어나면 이전 값으로 복원합니다. 따라서 예제의 Lumen 비교는 확산광 화면 추적을 끈 상태로 이루어집니다. 이 설정만으로 프로젝트에 없는 하드웨어 레이 트레이싱 지원이 활성화되지는 않습니다.

독립 검증에 사용한 설정은 [showcase-renderer-settings.ini](https://github.com/mazelines/UE6-PRT-GI/blob/ue6/Plugins/PRTGI/docs/showcase-renderer-settings.ini)에 있습니다. 필요한 항목만 프로젝트에 반영하세요. 이 파일은 자동으로 적용되지 않으며, 프로젝트 설정 전체를 이 파일로 덮어쓰면 안 됩니다.

예제는 UE DirectionalLight 하나를 공유합니다. 박스의 기존 면광원 영역은 개구부로 교체했으며, 관찰을 위한 전면 개구부는 유지했습니다.

아래 표의 캡처 경고 수는 과거 검증 기록이며, 현재 로컬 빌드 결과는 에디터에서 확인해야 합니다.

| 볼륨 | 빌드 및 검증 참고 |
|---|---|
| PRT_Global, PRT_ClassicBox, PRT_ColorBox, PRT_InvertedBox | 현재 v7 캐시는 최대 12회용 데이터를 로컬에서 생성. 런타임 기본값은 6회 |
| PRT_ShinyBox | 면당 16×16 해상도. 검사한 프로브 878개 중 21개를 캡처 경고와 함께 제외 |
| PRT_MetallicBox | 면당 16×16 해상도, 프로브 간격 40 cm, 전송 셀 크기 2 cm. 검사한 프로브 2,212개 중 77개를 캡처 경고와 함께 제외 |

곡면의 원형 경계를 완화하는 `r.PRTGI.HemisphereTransition` 기본값은 0.2입니다. [곡면 보간 개선](https://github.com/mazelines/UE6-PRT-GI/wiki/Curved-Surface-Interpolation)에 코드와 전후 비교를 정리했습니다.

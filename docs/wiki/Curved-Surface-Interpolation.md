# 곡면의 원형 경계와 프로브 보간 개선

UE6 브랜치의 ColorBox 흰 구체에서 보이던 원형 경계를 조사하고, 기존 프로브 가중치 전환 기능을 기본으로 활성화했습니다. `r.PRTGI.HemisphereTransition` 기본값을 **0에서 0.2로 변경**한 수정입니다. Surfel 생성이나 다중 바운스 사전 계산을 바꾼 것은 아니며, 셰이더의 `smoothstep` 계산은 이미 구현되어 있었습니다.

## 증상과 원인

이 플러그인은 Surfel에서 얻은 전송 정보를 프로브의 구면 조화 함수(SH) 계수로 갱신하고, 화면의 각 표면에서 주변 프로브를 보간합니다. 따라서 화면의 원형 무늬를 Surfel 원판이 직접 보이는 현상으로 해석하면 원인을 놓칠 수 있습니다.

기존 설정에서는 수신 표면의 뒤쪽에 놓인 프로브를 즉시 제외했습니다. 구체에서는 표면 법선이 위치마다 바뀌므로, 특정 프로브가 앞쪽에서 뒤쪽으로 분류되는 경계가 원형으로 나타납니다. 그 경계에서 유한한 프로브 기여도가 갑자기 사라지는 것이 이번 A/B 비교로 확인한 주요 원인입니다.

**변경 전 — 전환 폭 0**

![변경 전: 흰 구체의 원형 경계](https://raw.githubusercontent.com/mazelines/UE5-PRT-GI/ue6/Plugins/PRTGI/docs/images/prt-gi-curved-before.png)

**변경 후 — 전환 폭 0.2**

![변경 후: 연속 가중치를 적용한 흰 구체](https://raw.githubusercontent.com/mazelines/UE5-PRT-GI/ue6/Plugins/PRTGI/docs/images/prt-gi-curved-after.png)

두 이미지는 같은 카메라·조명·노출 설정으로 촬영한 실제 에디터 화면입니다. 구체의 밝은 직사광 영역은 유지됩니다.

## 실제 코드 변경

[PRTGI.cpp](https://github.com/mazelines/UE5-PRT-GI/blob/ue6/Plugins/PRTGI/Source/PRTGI/Private/PRTGI.cpp)의 `CVarPRTHemisphereTransition` 기본값과 설명을 수정했습니다. 아래 코드는 변경 부분만 축약한 것입니다.

```cpp
// 이전
CVarPRTHemisphereTransition(TEXT("r.PRTGI.HemisphereTransition"), 0.f, ...);

// 변경
CVarPRTHemisphereTransition(TEXT("r.PRTGI.HemisphereTransition"), .2f, ...);
```

[DefaultEngine.ini](https://github.com/mazelines/UE5-PRT-GI/blob/ue6/Config/DefaultEngine.ini)의 프로젝트 설정에도 같은 값을 명시했습니다.

```ini
[ConsoleVariables]
r.PRTGI.HemisphereTransition=0.2
```

렌더 스레드는 이 값을 0~0.5 범위로 제한해 셰이더에 전달합니다. [PRTGIApply.usf](https://github.com/mazelines/UE5-PRT-GI/blob/ue6/Plugins/PRTGI/Shaders/Private/PRTGIApply.usf)의 `SampleVolume`에는 다음 계산이 이미 있었습니다. 이번 셰이더 파일 변경은 원인을 설명하는 주석 수정입니다.

```hlsl
float FrontDistance = dot(ProbePosition - Position, SurfaceNormal);
if (FrontDistance <= 0.0)
    continue;

float TransitionWidth = HemisphereTransition
    * min(CellSize.x, min(CellSize.y, CellSize.z));

if (TransitionWidth > 0)
    Weight *= smoothstep(0, TransitionWidth, FrontDistance);
```

`FrontDistance`는 수신 표면의 접평면을 기준으로 한 프로브의 부호 있는 거리입니다. 값이 양수인 영역에서 경계에 가까워질수록 가중치를 부드럽게 줄이고, 0 이하에서는 기존처럼 제외합니다. 전환 폭 0에서는 `smoothstep`을 건너뛰어 경계에서 기여도가 급변했습니다.

최소 프로브 간격이 100cm인 경우 전환 폭은 20cm입니다. 아래 값은 최종 정규화 이전에 기존 보간 가중치에 곱하는 계수입니다.

| 접평면 앞쪽 거리 | 곱하는 계수 |
|---|---:|
| 0cm 이하 | 프로브 제외 |
| 10cm | 0.5 |
| 20cm 이상 | 1.0 |

이후 기존 `ProbeVisibility` 검사로 벽 차폐를 반영하고, 누적 조명을 유효 가중치 합으로 나눕니다. 새 광선이나 전송 항을 추가하지 않으며, 이번 수정만 적용할 때는 **사전 계산을 다시 할 필요가 없습니다.** 다만 저장소에는 캐시를 올리지 않으므로 처음 내려받은 프로젝트에서는 반드시 각 볼륨의 **Build Precomputation**을 실행해야 합니다.

## 비교 방법과 결과

UE6 Win64 Development Editor에서 같은 카메라로 전환 폭 0과 0.2를 비교했습니다. 바운스 6회, 가시성 모드 1, 가시성 바이어스 1cm, 접촉 차폐 8표본·20cm를 유지했습니다.

| 비교 설정 | 관찰 결과 |
|---|---|
| 전환 폭 0 | 구체의 원형 경계 재현 |
| 전환 폭 0.2 | 원형 불연속이 크게 줄어듦 |
| 전환 폭 0.5 | 경계가 완화되지만, 더 작은 0.2를 기본값으로 선택 |
| 전환 폭 0.2, 가시성 검사 끔 | 벽·바닥 모서리에 빛샘 발생. 기본 설정으로 채택하지 않음 |
| 전환 폭 0.2, 접촉 차폐 끔 | 구체의 완화 효과 유지. 접촉 차폐가 원형 경계 제거의 원인은 아님 |

간접광만 출력한 선형 HDR에서 구체 상단 경계 영역의 불연속 진단값이 감소했습니다. 진단값은 휘도의 4방향 이산 라플라시안 절댓값이며, 해당 영역의 99백분위수가 **164.53 → 68.62**, 약 **58.3% 감소**했습니다. 같은 영역의 평균 휘도 변화는 약 -0.086%였습니다.

이는 특정 영역의 경계 변화 지표입니다. Path Tracing 기준 오차나 전체 화질이 58% 개선됐다는 뜻은 아닙니다. 설정당 HDR 한 장을 사용했으며 통계적 유의성은 평가하지 않았습니다. ClassicBox·ShinyBox·실내 전체 화면도 비교해 새로운 뚜렷한 평면 고리나 벽 너머 빛샘이 없는지 확인했습니다. 모든 장면에 대한 보장은 아닙니다.

에디터 빌드는 성공했습니다. 실행 중인 에디터에서는 기존 셰이더 경로에 새 콘솔 값을 적용해 확인했고, C++ 기본값 변경은 별도 접미사 모듈로 빌드했습니다. 에디터를 강제로 재시작하거나 사용자의 맵을 저장하지 않았습니다. 성능 측정과 새 Path Tracing 정확도 평가는 이번 범위에 포함하지 않았습니다.

측정 영역, 카메라, 이미지 해시, 비교 조건은 [검증 기록 JSON](https://github.com/mazelines/UE5-PRT-GI/blob/ue6/Plugins/PRTGI/Tools/validation_curved_surface.json)에 있습니다. 이전 v6·바운스 검증 기록은 전환 폭 0에서 측정한 과거 결과로 유지합니다.

## 에디터에서 재현하기

같은 카메라와 조명을 유지한 채 콘솔에서 한 줄씩 실행해 비교합니다.

```text
r.PRTGI.HemisphereTransition 0
r.PRTGI.HemisphereTransition 0.2
```

진단 후에는 다음 설정으로 복원합니다.

```text
r.PRTGI.HemisphereTransition 0.2
r.PRTGI.ProbeVisibility 1
r.PRTGI.IndirectOnly 0
r.PRTGI.Debug 0
```

## 검토한 연구와 적용 범위

2026년 10월 14일 조사에서 다음 자료를 검토했습니다. 이번 수정은 이 시스템들의 전체 구현을 이식한 작업이 아닙니다.

- [EA Frostbite — Shipping Dynamic Global Illumination in Frostbite, SIGGRAPH 2024](https://www.advances.realtimerendering.com/s2024/content/EA-GIBS2/Apers_Advances-s2024_Shipping-Dynamic-GI.pdf): 프로브 보간과 깊이 기반 가시성, Surfel 법선·위치 재구성을 별개의 문제로 다룹니다. 이번에도 가시성 검사를 유지하면서 수신 표면의 가중치 경계를 조정했습니다.
- [SurfelPlus, SIGGRAPH Posters 2025](https://doi.org/10.1145/3721250.3742969): Surfel 관리와 생성·셰이딩 통합 등을 다룬 2쪽 포스터입니다. 현재 SH 프로브의 반구 제외 경계를 직접 해결하는 방법으로 해석하지 않았습니다.
- [Split Radiance Cascades, arXiv v2, 2026-10-11](https://arxiv.org/abs/2607.20384v2): 희소 프로브와 공간·각도 해상도를 다루는 프리프린트입니다. 향후 구조 개선의 비교 대상이며, 이번 경계 문제에 즉시 대체 적용한 방식은 아닙니다.
- [DDGI production 연구](https://arxiv.org/abs/2009.10796v3)와 [NVIDIA RTXGI 보간 구현](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/main/rtxgi-sdk/shaders/ddgi/Irradiance.hlsl): 연속 가중치와 별도의 가시성 처리 원칙을 참고했습니다. RTXGI의 전체 wrap 가중치를 복사하지 않고, 현재의 양의 반구에서만 작동하는 `smoothstep` 경로를 활성화했습니다.

SH 표현 한계, 프로브 밀도 부족, 차폐 데이터 오류까지 해결한 것은 아닙니다. 다른 장면에서 남는 얼룩은 프로브 배치·캡처·가시성·재구성을 구분해서 조사해야 합니다.

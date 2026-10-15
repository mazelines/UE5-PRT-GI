# PRTGI 사용자 가이드

정적 장면에서 프로브와 사전 계산된 광 전달 데이터를 사용해 간접광을 계산하는 Unreal Engine 플러그인입니다. 현재 UE6 브랜치는 방향광 회전에 따른 실시간 조명 갱신, 최대 12회 간접광 반사와 독립적인 프로브 볼륨을 지원하며, 런타임 기본값은 6회입니다.

![PRT GI Cornell Box 예제](https://raw.githubusercontent.com/mazelines/UE6-PRT-GI/ue6/Plugins/PRTGI/docs/images/prt-gi-showcase-ue6-cfcd80c5.png)

## 시작하기

사전 계산 캐시는 Git 저장소에 포함하지 않습니다. 프로젝트를 처음 열면 각 `PRTProbeVolume`의 **Build Precomputation**을 실행해야 PRT 간접광을 사용할 수 있습니다.

처음 사용한다면 설치 → 빠른 시작 → 프로브 볼륨 순서로 읽으세요.

- [설치와 프로젝트 구성](https://github.com/mazelines/UE6-PRT-GI/wiki/Installation)
- [빠른 시작과 Lumen 비교](https://github.com/mazelines/UE6-PRT-GI/wiki/Quick-Start)
- [프로브 볼륨 배치와 설정](https://github.com/mazelines/UE6-PRT-GI/wiki/Probe-Volumes)
- [사전 계산과 빌드 상태](https://github.com/mazelines/UE6-PRT-GI/wiki/Precomputation)
- [콘텐츠·캐시·패키징](https://github.com/mazelines/UE6-PRT-GI/wiki/Content-and-Packaging)
- [지원 범위와 제한 사항](https://github.com/mazelines/UE6-PRT-GI/wiki/Limitations)
- [단축키와 콘솔 명령](https://github.com/mazelines/UE6-PRT-GI/wiki/Shortcuts)
- [곡면의 원형 경계와 프로브 보간 개선](https://github.com/mazelines/UE6-PRT-GI/wiki/Curved-Surface-Interpolation)
- [SurfelPlus 기반 표본 배분·접촉 차폐 실험](https://github.com/mazelines/UE6-PRT-GI/wiki/SurfelPlus-Experiments)

## 최근 UE6 브랜치 변경

프로브 기여도 진단(`Debug 5~7`), 선택적 적응형 전송 표본 배분, 반해상도 접촉 차폐를 추가했습니다. 새 v8 캐시는 표본 설정과 예비·실제 경로 통계를 기록하며, 기존 v3~v7 캐시는 균등 1개 설정에서 계속 읽습니다. 기본 6회 바운스와 기존 렌더링 설정은 유지합니다.

이번 검증에서 적응형 표본 배분의 품질 우위는 확인되지 않았고, 반해상도 성능 이득도 시점에 따라 달랐습니다. 옵션은 기본 비활성화이며, [사용법·비교 결과·한계](https://github.com/mazelines/UE6-PRT-GI/wiki/SurfelPlus-Experiments)를 함께 확인하세요.

## 프로젝트 구성

| 경로 | 용도 |
|---|---|
| `UE_PRT_GI.uproject` | 플러그인을 실행할 호스트 프로젝트 |
| `Source/UE_PRT_GI` | 호스트 게임 모듈 |
| `Config` | 프로젝트 설정 |
| `Content` | 호스트 프로젝트의 템플릿 콘텐츠 |
| `Plugins/PRTGI` | PRTGI 플러그인 소스·셰이더·예제·캐시 |
| `docs/wiki` | 이 Wiki의 Markdown 원본 |

현재 `ue6` 브랜치의 프로젝트 타깃은 `Unreal6_0`을 사용합니다. 원본 플러그인의 검증 기록은 Windows / UE 5.8 / DX12·SM6 / 디퍼드 렌더링 기준입니다. 현재 호스트 프로젝트의 빌드·실행 검증과는 구분해서 확인하세요.

## README

[한국어](https://github.com/mazelines/UE6-PRT-GI/blob/ue6/README.md) · [English](https://github.com/mazelines/UE6-PRT-GI/blob/ue6/README.en.md) · [简体中文](https://github.com/mazelines/UE6-PRT-GI/blob/ue6/README.zh-CN.md)

# PRTGI 사용자 가이드

정적 장면에서 프로브와 사전 계산된 광 전달 데이터를 사용해 간접광을 계산하는 Unreal Engine 플러그인입니다. 방향광을 회전하면 조명을 실시간으로 갱신하며, 간접광 반사 1~4회와 독립적인 프로브 볼륨을 지원합니다.

![PRT GI Cornell Box 예제](https://raw.githubusercontent.com/mazelines/UE5-PRT-GI/ue6/Plugins/PRTGI/docs/images/prt-gi-showcase.png)

## 시작하기

처음 사용한다면 설치 → 빠른 시작 → 프로브 볼륨 순서로 읽으세요.

- [설치와 프로젝트 구성](https://github.com/mazelines/UE5-PRT-GI/wiki/Installation)
- [빠른 시작과 Lumen 비교](https://github.com/mazelines/UE5-PRT-GI/wiki/Quick-Start)
- [프로브 볼륨 배치와 설정](https://github.com/mazelines/UE5-PRT-GI/wiki/Probe-Volumes)
- [사전 계산과 빌드 상태](https://github.com/mazelines/UE5-PRT-GI/wiki/Precomputation)
- [콘텐츠·캐시·패키징](https://github.com/mazelines/UE5-PRT-GI/wiki/Content-and-Packaging)
- [지원 범위와 제한 사항](https://github.com/mazelines/UE5-PRT-GI/wiki/Limitations)
- [단축키와 콘솔 명령](https://github.com/mazelines/UE5-PRT-GI/wiki/Shortcuts)

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

[한국어](https://github.com/mazelines/UE5-PRT-GI/blob/ue6/README.md) · [English](https://github.com/mazelines/UE5-PRT-GI/blob/ue6/README.en.md) · [简体中文](https://github.com/mazelines/UE5-PRT-GI/blob/ue6/README.zh-CN.md)

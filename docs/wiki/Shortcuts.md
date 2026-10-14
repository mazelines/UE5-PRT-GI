# 단축키와 콘솔 명령

에디터 또는 PIE 뷰포트를 클릭해 포커스를 준 뒤 사용하세요.

| 단축키 | 동작 |
|---|---|
| Ctrl+Alt+1 | 동적 GI 끄기. 이 예제에는 오프라인 베이크 조명이 없음 |
| Ctrl+Alt+2 | PRT GI |
| Ctrl+Alt+3 | Lumen GI |
| Ctrl+Alt+4 | PRT 간접 확산광만 보기 / 전체 화면 전환 |
| Ctrl+Alt+P | 프로브 표시 / 숨기기. 위치 마커 또는 조명 구체 표시는 볼륨의 Display Mode에 따름 |
| Ctrl+Alt+G | 간접광 반사 4 → 8 → 12회 순환. 해당 캐시 필요 |
| Ctrl+Alt+[ / ] | DirectionalLight Yaw를 기본 간격인 5°만큼 감소 / 증가 |
| Ctrl+Alt+Shift+[ / ] | DirectionalLight Pitch를 기본 간격인 5°만큼 감소 / 증가 |
| Ctrl+Alt+B | 이전 방식의 전역 베이크 명령. 액터 볼륨은 빌드하지 않음 |

지역 사전 계산은 해당 `PRTProbeVolume`을 선택하고 **Build Precomputation**을 눌러 실행합니다. 간접광 반사 기본값은 6회입니다. 단축키 비교 후 기본값으로 돌아가려면 `r.PRTGI.Bounces 6`을 실행하세요.

포커스나 키보드 배열 때문에 단축키가 작동하지 않으면 UE 콘솔에서 다음 명령을 하나씩 실행하세요.

```text
PRTGI.SetMode PRT
PRTGI.SetMode Lumen
PRTGI.ToggleIndirectOnly
r.PRTGI.ShowProbes 1
r.PRTGI.Bounces 6
PRTGI.RotateLight 5 0
PRTGI.Status
```

`PRTGI.SetMode Offline`은 동적 GI를 종료하는 이전 명령 이름입니다. 오프라인 조명이나 Path Tracing 기준 이미지를 자동으로 생성하지는 않습니다.

곡면의 프로브 경계 비교에는 `r.PRTGI.HemisphereTransition 0`을 사용하고, 확인 후 기본값인 `r.PRTGI.HemisphereTransition 0.2`로 복원하세요. 이 설정은 다시 사전 계산할 필요 없이 적용됩니다. [코드와 비교 결과](https://github.com/mazelines/UE5-PRT-GI/wiki/Curved-Surface-Interpolation)를 참고하세요.

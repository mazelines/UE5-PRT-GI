# 프로브 볼륨 배치와 설정

1. 레벨을 저장합니다. 사전 계산에 사용하는 지오메트리와 머티리얼은 정적 상태를 유지해야 합니다.
2. UE **DirectionalLight**를 추가하고, 실시간으로 회전할 수 있도록 Mobility를 **Movable**로 설정합니다. 방향광이 여러 개라면 사용할 광원의 Actor Tag에 `PRTGI.PrimaryLight`를 추가합니다.
3. Place Actors에서 **PRT Probe Volume**을 찾아 장면에 배치합니다.
4. 볼륨 범위, 프로브 간격, Near Clip, Priority를 설정합니다.
5. PRT GI를 켠 뒤 액터의 Details → **PRT → Build**에서 **Build Precomputation**을 누릅니다.
6. 완료되면 **PRT → Status**의 **Cache Status**, **GI Active**, **Effective Bounces**, **Build Warnings**를 확인하고 레벨을 저장합니다.

볼륨은 **PRT GI 모드**에서만 간접광에 기여하고 프로브를 표시합니다. Lumen으로 전환하면 PRT GI의 기여는 꺼집니다.

### 설정 항목

| 항목 | 설명 |
|---|---|
| Enabled | 볼륨 활성화 여부 |
| Extent | 각 축 방향 반쪽 크기(cm). 액터 스케일도 범위에 반영됨 |
| Probe Spacing | 각 축의 목표 최대 간격(cm). 실제 균등 배치 간격은 Actual Spacing에서 확인 |
| Probe Inset | 볼륨 경계에서 프로브 격자를 안쪽으로 들이는 거리(cm) |
| Priority | 국소적으로 촘촘히 배치한 영역에는 더 높은 우선순위 사용 |
| Near Clip Cm | 프로브 캡처의 근거리 클리핑 거리. 기본 1 cm, 범위 0.01~10 cm. 주 카메라의 Near Clip과 별개 |
| Capture Resolution | 캡처 면당 해상도. 기본 16, 범위 4~32. 높일수록 캡처 비용 증가 |
| Transport Cell Size | 정적 표면을 묶는 단위 크기. 기본 2 cm, 범위 1~100 cm. 작을수록 대체로 데이터와 계산량 증가 |
| Blend Face / Distance | 전환에 사용할 면 하나와 거리(cm). 0=없음, 1/2=−/+X, 3/4=−/+Y, 5/6=−/+Z. 전환이 필요한 개구부에만 설정 |
| Display Mode | Hidden, Positions, Lighting. Lighting은 각 프로브의 조명 색상 표시 |
| Probe Radius | 시각화 구체의 반지름(cm). 실제 샘플링 위치에는 영향 없음 |

볼륨은 현재 **축에 정렬된 형태**이며 회전은 지원하지 않습니다. 각 축에는 최대 32개의 프로브를 배치할 수 있습니다. 한도를 넘으면 간격을 넓히거나 볼륨을 나누세요.

별도로 볼륨당 **캡처 샘플 4,000,000개** 제한이 있습니다. `ProbeCounts.X × ProbeCounts.Y × ProbeCounts.Z × 6 × CaptureResolution²`가 이 값을 넘으면 안 됩니다. 기본 해상도 16에서는 축별 제한을 지키면서 총 2,604개까지 배치할 수 있습니다. 한도를 넘으면 캡처 해상도를 낮추거나, 프로브 간격을 넓히거나, 볼륨을 나누세요.

처음에는 전역 볼륨을 Priority=0, 간격 약 200 cm로, 지역 볼륨을 Priority=10, 간격 약 30~40 cm로 설정한 뒤 장면 크기에 맞춰 조정하세요. 프로브를 벽 안에 배치하거나 막힌 벽을 가로질러 블렌딩 전환을 설정하지 마세요. Near Clip이 너무 크면 프로브 가까이의 지오메트리가 잘려 가시성 오류나 빛샘이 생길 수 있습니다.

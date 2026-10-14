# 콘텐츠·캐시·패키징

사전 계산된 `.surfels`, `.surfels.multi`, `.prtgi` 캐시는 Git 저장소에 올리지 않습니다. 프로젝트를 처음 내려받으면 에디터에서 각 `PRTProbeVolume`의 **Build Precomputation**을 직접 실행하세요. 아래 캐시 경로는 로컬 빌드 후 생성되는 위치입니다.

아래 경로는 플러그인 폴더 `Plugins/PRTGI`를 기준으로 합니다.

- `Content/Maps`: 예제 레벨.
- `Content/Showcase/Materials`, `Meshes` 등: 예제에 필요한 프로젝트 에셋의 복사본. 참조는 플러그인 내부 에셋으로 연결됩니다.
- `Content/Materials`: 프로브 시각화와 지오메트리 캡처용 머티리얼.
- `Content/Precomputed/Volumes`: 레벨 패키지 이름과 볼륨 GUID별로 구분한 `.surfels` / `.multi` 원시 캐시. 광원 매개변수는 포함하지 않습니다.

`.surfels`는 캡처한 지오메트리를, 짝이 되는 `.multi`는 다중 반사 전송 데이터를 저장합니다. 두 파일을 함께 보관하세요. 원시 바이너리 파일이므로 콘텐츠 브라우저에 `.uasset`으로 표시되지 않습니다. 캐시는 고정된 태양 각도의 최종 이미지가 아닙니다. 런타임에도 현재 광원 입력과 가림을 계산한 뒤 사전 계산된 전송 데이터를 적용합니다.

`/PRTGI/` 아래의 플러그인 레벨은 플러그인 내부의 `Content/Precomputed/Volumes/<레벨-패키지>/<GUID>.prtgi.surfels`와 `<GUID>.prtgi.surfels.multi`에 저장합니다. `/Game/`을 포함한 다른 레벨은 `YourProject/Content/PRTGI/Volumes/<레벨-패키지>/`에 저장합니다. 레벨 패키지 이름의 슬래시는 밑줄로 바뀝니다. 레벨을 이동하거나 이름을 바꾸면 캐시 경로도 바뀌므로 **Cache File**을 확인하고 필요하면 다시 빌드하세요.

플러그인의 빌드 규칙은 자체 `Content/Precomputed` 폴더를 별도의 **NonUFS** 파일로 패키징합니다. `/Game/` 레벨을 사용하는 프로젝트에서는 **Additional Non-Asset Directories To Copy**에 `PRTGI/Volumes`를 추가해 프로젝트의 `Content/PRTGI/Volumes`도 포함해야 합니다. 입력 경로는 프로젝트 Content 기준입니다. 기존 검증 기록은 에디터 사용을 대상으로 하므로 패키징 결과에서도 캐시 로드를 확인하세요.

#pragma once
inline const char* OgcInputHelp(){return R"MMHELP(mapMake 서버 설정 및 클라이언트 XML 생성

입력 폴더: 검색할 원본 SHP/래스터의 PC 경로. 하위 폴더를 모두 검색합니다.
래스터 확장자: tif;img;dem 등의 확장자를 세미콜론으로 입력합니다. SHP는 별도로 검색합니다.
원본 EPSG: 선택 행의 데이터 좌표계 코드입니다. 입력 후 '선택 행에 적용'을 누릅니다.
WFS 고유 ID 필드: 중복되지 않는 DBF 필드(예: UID)를 지정하거나 비워 둡니다. 선택 행에 적용합니다.
  빈 값은 원본 DBF 변경 없이 OGR 레코드 번호를 가상 ID로 사용합니다. 데이터 편집·재정렬 후 ID는 바뀔 수 있습니다.
지도 EPSG: MapServer MAP 전체 출력 좌표계입니다. 원본 EPSG와 달라도 됩니다.
좌표 단위: 지도 EPSG에 맞춰 DD(경위도), METERS, FEET를 지정합니다.
지도 범위: 지도 EPSG 기준 minX minY maxX maxY입니다.
벡터 RGB: 기본 도형 색상. 선택 행에 적용합니다. SLD를 연결하면 SLD 스타일이 우선합니다.
서버 데이터 루트: MapServer가 원본 데이터 파일을 실제로 읽는 절대 폴더입니다. URL이나 출력 폴더가 아닙니다.
  입력 폴더 기준 상대경로를 서버 데이터 루트 뒤에 붙여 .map의 DATA 경로를 만듭니다.
  예1: 입력 D:/map/shp/EAR_100M, 서버 루트 D:/map/shp/EAR_100M → D:/map/shp/EAR_100M/LAP030.shp
  예2: 입력 D:/map/shp, 서버 루트 D:/map/shp → D:/map/shp/EAR_100M/LAP030.shp
  서버가 다른 PC면 그 서버에서 원본을 배치한 경로를 입력하고 같은 하위 폴더 구조로 복사합니다.
서버 .map 경로: 서버에 배치할 mapserver.map의 경로입니다. 실제 배치 위치와 일치해야 합니다.
  현재 MS4W 예: /ms4w/apps/local-demo/mapserver.map (MS4W의 허용 경로 규칙에 맞춤)
내부 WMS URL: MapCache가 원본 WMS 영상을 요청하는 MapServer 주소입니다.
  같은 서버 예: http://127.0.0.1/cgi-bin/mapserv.exe
외부 서비스 URL: C4IMap이 접속할 WMS/WFS MapServer 주소입니다.
  같은 PC 예: http://127.0.0.1/cgi-bin/mapserv.exe, 다른 PC: 127.0.0.1 대신 서버 IP.
외부 WMTS URL: C4IMap이 접속할 MapCache WMTS 주소입니다.
  현재 예제 별칭: http://127.0.0.1/ear100-cache/wmts
  해당 별칭이 가리키는 MapCache 설정에 생성한 mapcache.xml을 배치해야 합니다.
  세 URL에는 ?map=, SERVICE 등 쿼리 인수를 입력하지 않습니다. 생성기가 필요한 인수를 붙입니다.
서버 캐시 경로: MapCache가 타일을 저장할 서버 폴더입니다. 예: D:/ms4w/tmp/mapmake-cache
출력 폴더: PC에서 새 생성 결과 묶음을 저장할 폴더입니다. 서버 배치는 자동 실행하지 않습니다.
적용할 기존 SLD: 이미 만든 SLD를 선택하면 이 스타일을 우선 적용합니다. 별도 SLD 생성은 생략합니다.

생성물:
WMS 체크 → wms-sources.xml
WFS 체크 → wfs-sources.xml (벡터만)
WMTS 체크 → wmts-sources.xml 및 서버용 mapcache.xml
항상 mapserver.map을 생성합니다. WMS는 현재 UI에서 WMTS 공급원을 위해 활성화되어 있습니다.
C4IMap에서 사용할 서비스 XML 한 개를 골라 실행 파일 옆 ogc-sources.xml로 복사합니다.
XML 지도소스 설정에서 직접 불러오기도 가능합니다. XML만 복사해도 서버 설치가 되는 것은 아닙니다.

SLD:
SLD 생성 체크 + XML 스타일 없음 → 기본 색상의 styles.sld 자동 생성(.map에는 동등한 기본 스타일 포함).
SLD 생성 체크 + XML 스타일 가져오기/메인 적용 → XML/GSS 기반 SLD 자동 생성 후 .map에 자동 연결.
기존 SLD 선택 → 선택한 SLD를 우선 연결. MapServer 8.2 이상 필요.
여러 축척의 표준 XML을 동시에 연 경우에는 기존 안내대로 선택 문서 SLD를 지정해야 합니다.
심볼 기본 URL이 비어 있으면 D:/map/styles/symbols의 공통 PNG를 사용합니다.
자동 생성된 SLD를 MAP에 연결한 경우 imported-style_N.sld도 함께 서버에 배치하세요.

트리 이름:
가져온 XML의 이름·분류·Visible을 우선 사용합니다.
없으면 EAR_100M/AIR_200M 등 데이터 폴더 이름을 이용해 D:/map/styles의 축척 XML을 찾습니다.
축척 XML에 연결되지 않은 항목은 파일명과 '일반' 분류를 사용합니다.
표시 이름과 서버 LayerName은 분리되며, 속성 조회에는 실제 서버 레이어 코드를 사용합니다.
WMTS는 WFS가 체크되어 있으면 WFS로, 아니면 WMS GetFeatureInfo로 벡터 속성을 조회합니다.
)MMHELP";}

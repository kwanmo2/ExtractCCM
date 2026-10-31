"""Generates src/ui/Translations_ko.inc from the Korean dictionary below.

Every tr() string in src/ui/*.cpp and every core string listed in CORE must have an entry;
the script fails otherwise, so new UI text cannot silently stay untranslated.
Usage: python tools/gen_translations.py
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

KO = {
    # ---- menus / window
    "ExtractCCM": "ExtractCCM",
    "&File": "파일(&F)",
    "&View": "보기(&V)",
    "&Help": "도움말(&H)",
    "Quit": "종료",
    "Fit image": "이미지 맞춤",
    "How to use": "사용 방법",
    "Language": "언어",
    "1. Capture the chart with the camera's CCM and gamma disabled, evenly lit, no clipping.\n"
    "2. Open the image and set the data bits and black level.\n"
    "3. Choose the illuminant and chart; load the chart's reference file if required.\n"
    "4. Click the CENTERS of four corner patches in order:\n"
    "   1 = first patch (top-left), 2 = end of first row,\n"
    "   3 = last patch, 4 = start of last row.\n"
    "   Drag handles to adjust; use Rotate/Flip if the grid is mis-ordered.\n"
    "5. Choose the model and constraints, then Compute.\n\n"
    "Zoom: mouse wheel. Pan: right/middle drag or Ctrl+drag.":
    "1. 카메라의 CCM과 감마를 끄고, 조명이 고르게 닿고 포화되지 않도록 차트를 촬영합니다.\n"
    "2. 이미지를 열고 데이터 비트와 블랙 레벨을 설정합니다.\n"
    "3. 조명과 차트를 선택하고, 필요하면 차트 기준값 파일을 불러옵니다.\n"
    "4. 모서리 패치 4개의 '중심'을 순서대로 클릭합니다:\n"
    "   1 = 첫 패치(좌상단), 2 = 첫 행의 끝,\n"
    "   3 = 마지막 패치, 4 = 마지막 행의 시작.\n"
    "   핸들을 드래그해 조정하고, 격자 순서가 틀리면 회전/뒤집기를 사용합니다.\n"
    "5. 모델과 제약 조건을 고른 뒤 계산합니다.\n\n"
    "확대/축소: 마우스 휠. 이동: 오른쪽/가운데 버튼 드래그 또는 Ctrl+드래그.",
    "Open a chart image (camera CCM and gamma OFF), then click the centers of patches 1 → 2 → 3 → 4 "
    "as shown in the chart panel.":
    "차트 이미지(카메라 CCM·감마 OFF)를 열고, 패치 1 → 2 → 3 → 4의 중심을 차례로 클릭하세요.",

    # ---- point mapping mode
    "Color chart": "컬러 차트",
    "Point mapping (no chart)": "대응점 맵핑 (차트 없음)",
    "Point mapping: click the same surface points in a reference image (e.g. phone photo)\n"
    "and in the camera image; the CCM is fitted to the reference colors.":
    "대응점 맵핑: 기준 이미지(예: 휴대폰 사진)와 카메라 이미지에서 같은 표면 지점을 클릭하면,\n"
    "기준 이미지의 색에 맞춰 CCM을 계산합니다.",
    "Point mapping: open the reference image (left) and the camera image (right), then click matching points.":
    "대응점 맵핑: 기준 이미지(왼쪽)와 카메라 이미지(오른쪽)를 열고 같은 지점을 차례로 클릭하세요.",
    "4. Reference image / point pairs": "4. 기준 이미지 / 대응점",
    "Open reference image…": "기준 이미지 열기…",
    "Open reference image": "기준 이미지 열기",
    "No reference image": "기준 이미지 없음",
    "%1\n%2 x %3": "%1\n%2 x %3",
    "Reference encoding": "기준 인코딩",
    "sRGB (phone JPEG/PNG)": "sRGB (휴대폰 JPEG/PNG)",
    "Gamma 2.2": "감마 2.2",
    "Linear": "선형",
    "Sample area": "샘플 영역",
    "ref ±": "기준 ±",
    "cam ±": "카메라 ±",
    "Neutralize the reference with gray points (recommended)": "회색 점으로 기준 이미지 중립화 (권장)",
    "Removes the color cast of the reference image: points marked as gray\nbecome neutral in the target.":
    "기준 이미지의 색 틀어짐을 제거합니다: 회색으로 표시한 점이\n목표값에서 무채색이 됩니다.",
    "Match brightness too": "밝기까지 맞춤",
    "Off (recommended): only the reference color (hue/saturation) is used;\n"
    "brightness follows the camera. Reflections and exposure differences\n"
    "in the reference image make its brightness unreliable.":
    "끄기(권장): 기준 이미지의 색(색상/채도)만 사용하고 밝기는 카메라를 따릅니다.\n"
    "기준 이미지는 반사와 노출 차이 때문에 밝기를 신뢰하기 어렵습니다.",
    "Click a point in one image, then the same surface point in the other image.\n"
    "Esc cancels, Delete removes the selected pair, drag a marker to move it.\n"
    "Mark white/gray surfaces as 'Gray'. Use flat, evenly lit areas.":
    "한쪽 이미지에서 점을 클릭한 뒤, 다른 쪽 이미지에서 같은 표면 지점을 클릭하세요.\n"
    "Esc: 취소, Delete: 선택한 쌍 삭제, 마커 드래그: 이동.\n"
    "흰색/회색 표면은 '회색'으로 표시하고, 평평하고 고르게 밝은 영역을 고르세요.",
    "Gray": "회색",
    "Ref": "기준",
    "Cam": "카메라",
    "Ref σ %": "기준 σ %",
    "Cam σ %": "카메라 σ %",
    "Clip %": "포화 %",
    "Delete pair": "쌍 삭제",
    "Clear all": "전체 삭제",
    "Remove all %1 point pairs?": "대응점 %1쌍을 모두 삭제할까요?",
    " G": " 회",
    "Now click the same point in the camera image (Esc cancels).": "이제 카메라 이미지에서 같은 지점을 클릭하세요 (Esc: 취소).",
    "Now click the same point in the reference image (Esc cancels).": "이제 기준 이미지에서 같은 지점을 클릭하세요 (Esc: 취소).",
    "Pair %1 added. Mark it as 'Gray' in the table if it is a white/gray surface.":
    "%1번 쌍을 추가했습니다. 흰색/회색 표면이면 표에서 '회색'에 체크하세요.",
    "Open a reference image first.": "먼저 기준 이미지를 여세요.",
    "Need at least 4 point pairs (8 or more recommended).": "대응점이 4쌍 이상 필요합니다 (8쌍 이상 권장).",
    "\nPoint mapping: %1 points, reference %2, output %3, %4\n": "\n대응점 맵핑: %1개 점, 기준 %2, 출력 %3, %4\n",
    "brightness matched": "밝기 포함",
    "chromaticity only": "색감만",
    "Not available in point mapping (reference brightness is not used).\n":
    "대응점 맵핑에서는 사용할 수 없습니다 (기준 이미지 밝기를 쓰지 않음).\n",
    "== Point mapping ==\n": "== 대응점 맵핑 ==\n",
    "Points used: %1 (gray: %2)\n": "사용한 점: %1개 (회색 %2개)\n",
    "Reference neutralization gains R/G/B: %1 / %2 / %3\n": "기준 이미지 중립화 게인 R/G/B: %1 / %2 / %3\n",
    "WARNING: no gray point - white balance is taken from all points (gray world) and may be off.\n":
    "경고: 회색 점이 없습니다. 모든 점의 평균으로 화이트밸런스를 잡으므로 틀어질 수 있습니다.\n",
    "WARNING: only %1 points - the CCM is poorly constrained. Use 8+ points with different colors.\n":
    "경고: 점이 %1개뿐이라 CCM이 충분히 결정되지 않습니다. 서로 다른 색으로 8개 이상 찍으세요.\n",
    "Excluded (clipped in either image): %1\n": "제외됨 (어느 한쪽 이미지에서 포화): %1\n",
    "High variation inside the sample area (texture/edge?): %1\n": "샘플 영역 안의 편차가 큼 (질감/경계?): %1\n",

    # ---- 1. image
    "1. Image": "1. 이미지",
    "Open image…": "이미지 열기…",
    "Open RAW…": "RAW 열기…",
    "No image": "이미지 없음",
    "%1\n%2 x %3, %4-bit container": "%1\n%2 x %3, %4비트 컨테이너",
    "Data bits": "데이터 비트",
    "Significant bits of the pixel data. White level = 2^bits - 1.\n"
    "Use 16 for MSB-aligned data in a 16-bit container.":
    "픽셀 데이터의 유효 비트 수. 화이트 레벨 = 2^비트 - 1.\n"
    "16비트 컨테이너에 MSB 정렬된 데이터라면 16을 사용하세요.",
    "Auto": "자동",
    "Display gain": "표시 게인",
    "Open image": "이미지 열기",
    "Open chart image": "차트 이미지 열기",
    "Images (*.bmp *.png *.tif *.tiff *.jpg *.jpeg *.pgm *.ppm);;All files (*)":
    "이미지 (*.bmp *.png *.tif *.tiff *.jpg *.jpeg *.pgm *.ppm);;모든 파일 (*)",
    "Open RAW": "RAW 열기",
    "RAW (*.raw *.bin);;All files (*)": "RAW (*.raw *.bin);;모든 파일 (*)",

    # ---- RAW dialog
    "Open headerless RAW": "헤더 없는 RAW 열기",
    "8-bit (1 byte/sample)": "8비트 (샘플당 1바이트)",
    "10-16 bit (2 bytes/sample, little endian)": "10~16비트 (샘플당 2바이트, 리틀 엔디언)",
    "Interleaved RGB": "인터리브 RGB",
    "Interleaved BGR": "인터리브 BGR",
    "Mono": "모노",
    "Width": "너비",
    "Height": "높이",
    "Sample size": "샘플 크기",
    " % of pitch": " % (패치 간격 대비)",
    "Header bytes": "헤더 바이트",
    "Layout": "배열",

    # ---- 2. black level
    "2. Black level / clipping": "2. 블랙 레벨 / 포화",
    "Manual (per channel DN)": "수동 (채널별 DN)",
    "Estimate from chart (regression)": "차트에서 추정 (회귀)",
    "Zero": "0",
    "Mode": "방식",
    "Black": "블랙",
    "Clip threshold": "포화 기준",
    "Pixels at or above this fraction of the white level count as clipped.\n"
    "Patches with more than 1% clipped pixels are excluded.":
    "화이트 레벨의 이 비율 이상인 픽셀은 포화로 간주합니다.\n"
    "포화 픽셀이 1%를 넘는 패치는 제외됩니다.",

    # ---- 3. illuminant
    "3. Illuminant": "3. 조명",
    "Light": "광원",
    "Custom CCT…": "사용자 지정 색온도…",
    "Custom CCT": "사용자 색온도",
    "x = %1, y = %2, CCT ≈ %3 K\n%4": "x = %1, y = %2, 색온도 ≈ %3 K\n%4",
    "White balance to output white (recommended)": "출력 백색으로 화이트밸런스 (권장)",
    "On: neutrals map to neutral output (CCM includes white balance).\n"
    "Off: keep the color cast of the illuminant in the target.":
    "켜기: 회색이 무채색으로 출력됩니다 (CCM에 화이트밸런스 포함).\n"
    "끄기: 목표값에 조명의 색 캐스트를 그대로 둡니다.",
    "Output space": "출력 색공간",

    # ---- 4. chart
    "4. Color chart": "4. 컬러 차트",
    "Chart": "차트",
    "rows ": "행 ",
    "cols ": "열 ",
    "Grid": "격자",
    "Load reference…": "기준값 불러오기…",
    "CGATS (LAB_L/LAB_A/LAB_B or XYZ, D50) or CSV with [name,]L,a,b rows.":
    "CGATS (LAB_L/LAB_A/LAB_B 또는 XYZ, D50) 또는 [이름,]L,a,b 행으로 된 CSV.",
    "Row-major": "행 우선",
    "Column-major": "열 우선",
    "Reference": "기준값",
    "Auto orientation": "자동 방향",
    "Tries all 8 corner orders (rotations and mirror) and keeps the one whose\n"
    "patch colors fit the chart reference best.":
    "코너 순서 8가지(회전·좌우반전)를 모두 시험해, 패치 색이 차트 기준값과\n"
    "가장 잘 맞는 순서를 선택합니다.",
    "Patch order": "패치 순서",
    "Fix the patch order automatically when computing": "계산 시 패치 순서 자동 보정",
    "If the clicked corners are rotated or mirrored relative to the chart,\n"
    "the order that fits the reference best is used instead.":
    "클릭한 코너가 차트에 비해 회전되거나 반전되어 있으면,\n"
    "기준값과 가장 잘 맞는 순서로 바꿔서 계산합니다.",
    "Auto orientation applied: rotated %1 x 90°%2 (chart fit ΔE2000 %3 instead of %4).":
    "자동 방향 적용: %1 x 90° 회전%2 (차트 피팅 ΔE2000 %4 → %3).",
    "The patch order looks wrong (chart rotated or mirrored?). With 'Auto orientation' "
    "the chart fits with ΔE2000 %1 instead of %2.":
    "패치 순서가 잘못된 것 같습니다 (차트가 회전 또는 반전?). '자동 방향'을 쓰면 "
    "차트 피팅 ΔE2000이 %2 대신 %1이 됩니다.",
    "Auto orientation: rotated %1 x 90°%2, chart fit ΔE2000 %3 (next best %4)":
    "자동 방향: %1 x 90° 회전%2, 차트 피팅 ΔE2000 %3 (차선 %4)",
    ", mirrored": ", 반전",
    "WARNING: ": "경고: ",
    "Rotate 90°": "90° 회전",
    "Flip": "뒤집기",
    "Clear corners": "코너 지우기",
    "Reference: %1": "기준값: %1",
    "Built-in reference.": "내장 기준값.",
    "<b>Reference file required.</b>": "<b>기준값 파일이 필요합니다.</b>",
    "%1 patches, %2 neutral. %3<br>%4": "패치 %1개, 회색 %2개. %3<br>%4",
    "Load chart reference": "차트 기준값 불러오기",
    "Reference (*.txt *.cgats *.it8 *.cie *.csv);;All files (*)":
    "기준값 (*.txt *.cgats *.it8 *.cie *.csv);;모든 파일 (*)",
    "Load reference": "기준값 불러오기",

    # ---- 5. model
    "5. CCM model": "5. CCM 모델",
    "Model": "모델",
    "Linear RGB least squares": "선형 RGB 최소제곱",
    "Minimize ΔE2000 (iterative)": "ΔE2000 최소화 (반복)",
    "Objective": "목적 함수",
    "Separate exposure gain k from matrix": "노출 게인 k를 행렬에서 분리",
    "Report the exposure gain separately so that matrix coefficients stay small.\n"
    "Apply k with the sensor/ISP gain.":
    "노출 게인을 따로 출력해 행렬 계수를 작게 유지합니다.\n"
    "k는 센서/ISP 게인으로 적용하세요.",
    "Limit": "제한",
    "Coefficients": "계수",
    "Free (±)": "자유 (±)",
    "Non-negative only": "0 이상만",
    "Offset": "오프셋",
    "Relative weight of gray patches in the fit.": "피팅에서 회색 패치의 상대 가중치.",
    "Neutral weight": "회색 가중치",

    # ---- 6. registers
    "6. Hardware registers": "6. 하드웨어 레지스터",
    "Quantize to fixed point": "고정소수점으로 양자화",
    "int ": "정수 ",
    "frac ": "소수 ",
    "signed": "부호",
    "Format": "형식",
    "Preserve row sums when rounding": "반올림 시 행 합계 유지",
    " bit": " 비트",
    "Offset DN depth": "오프셋 DN 비트",

    # ---- compute / export
    "Compute CCM": "CCM 계산",
    "Compute": "계산",
    "Export": "내보내기",
    "JSON": "JSON",
    "CSV": "CSV",
    "C header": "C 헤더",
    "Registers": "레지스터",
    "Save corrected image…": "보정 이미지 저장…",
    "Open a chart image first.": "먼저 차트 이미지를 여세요.",
    "Click the centers of the four corner patches first.": "먼저 모서리 패치 4개의 중심을 클릭하세요.",
    "This chart needs a reference file (Load reference…).": "이 차트는 기준값 파일이 필요합니다 (기준값 불러오기…).",
    "Compute failed.": "계산에 실패했습니다.",
    "Settings changed - press Compute CCM to update the result.": "설정이 바뀌었습니다. CCM 계산을 눌러 결과를 갱신하세요.",
    "ΔE2000 mean %1, max %2 over %3 patches (WB only: %4)": "ΔE2000 평균 %1, 최대 %2 (패치 %3개, WB만: %4)",
    "Compute the CCM first.": "먼저 CCM을 계산하세요.",
    "Cannot write %1": "%1에 쓸 수 없습니다",
    "Cannot open %1": "%1을(를) 열 수 없습니다",
    "Saved %1": "%1 저장됨",
    "Saved %1 (sRGB-encoded, 8-bit)": "%1 저장됨 (sRGB 인코딩, 8비트)",
    "CSV (*.csv)": "CSV (*.csv)",
    "C/C++ header (*.h)": "C/C++ 헤더 (*.h)",
    "Text (*.txt)": "텍스트 (*.txt)",
    "JSON (*.json)": "JSON (*.json)",
    "Save": "저장",
    "Save corrected image": "보정 이미지 저장",
    "Images (*.png *.bmp *.tif)": "이미지 (*.png *.bmp *.tif)",
    "No result to export.": "내보낼 결과가 없습니다.",

    # ---- session
    "Load session…": "세션 불러오기…",
    "Save session…": "세션 저장…",
    "Load session": "세션 불러오기",
    "Save session": "세션 저장",
    "CCM session (*.ccmproj)": "CCM 세션 (*.ccmproj)",
    "Invalid session file.": "잘못된 세션 파일입니다.",
    "Session has no image or no four corners.": "세션에 이미지나 코너 4개가 없습니다.",

    # ---- result panel
    "Matrix": "행렬",
    "Patches": "패치",
    "Swatches (target / output)": "색 견본 (목표 / 출력)",
    "Corrected image": "보정 이미지",
    "Diagnostics": "진단",
    "#": "#",
    "Name": "이름",
    "Use": "사용",
    "Cam R": "카메라 R",
    "Cam G": "카메라 G",
    "Cam B": "카메라 B",
    "Target": "목표",
    "Output": "출력",
    "ΔE00": "ΔE00",
    "ΔE76": "ΔE76",
    "ΔC": "ΔC",
    "ΔE00 WB-only": "ΔE00 WB만",
    "Sat %": "포화 %",
    "Pixels": "픽셀",

    # ---- summary
    "\nΔE2000 : mean %1  median %2  max %3  (%4 patches)\n": "\nΔE2000 : 평균 %1  중앙값 %2  최대 %3  (패치 %4개)\n",
    "ΔE76   : mean %1  max %2\n": "ΔE76   : 평균 %1  최대 %2\n",
    "ΔC     : mean %1  max %2\n": "ΔC     : 평균 %1  최대 %2\n",
    "WB only: ΔE2000 mean %1  max %2\n": "WB만   : ΔE2000 평균 %1  최대 %2\n",
    "\nmax |coef| %1   condition number %2   row norms %3 / %4 / %5\n":
    "\n최대 |계수| %1   조건수 %2   행 노름 %3 / %4 / %5\n",
    "\nWARNING: some coefficients exceed the register range and were clipped. "
    "Enable 'Limit' with the register range or separate the exposure gain.\n":
    "\n경고: 일부 계수가 레지스터 범위를 넘어 잘렸습니다. "
    "'제한'을 레지스터 범위로 켜거나 노출 게인을 분리하세요.\n",
    "\nIlluminant: %1 (%2 K), output %3, %4\n": "\n조명: %1 (%2 K), 출력 %3, %4\n",
    "white balanced": "화이트밸런스 적용",
    "illuminant cast kept": "조명 색 캐스트 유지",

    # ---- diagnostics
    "== Linearity (neutral patches) ==\n": "== 선형성 (회색 패치) ==\n",
    "Black estimate (DN): %1 / %2 / %3\n": "블랙 추정 (DN): %1 / %2 / %3\n",
    "Linear fit R² (worst channel): %1\n": "선형 피팅 R² (최저 채널): %1\n",
    "Response exponent: %1 (1.0 = linear)\n": "응답 지수: %1 (1.0 = 선형)\n",
    "WARNING: response is not linear - camera gamma/LUT may be ON. A CCM must be applied to linear data.\n":
    "경고: 응답이 선형이 아닙니다. 카메라 감마/LUT가 켜져 있을 수 있습니다. CCM은 선형 데이터에 적용해야 합니다.\n",
    "WARNING: poor linear fit - check uneven lighting, glare or clipping.\n":
    "경고: 선형 피팅이 나쁩니다. 조명 불균일, 반사, 포화를 확인하세요.\n",
    "Not enough neutral patches.\n": "회색 패치가 부족합니다.\n",
    "\n== Clipping ==\n": "\n== 포화 ==\n",
    "Patch %1 (%2): %3 % clipped - excluded\n": "패치 %1 (%2): %3 % 포화 - 제외됨\n",
    "None.\n": "없음.\n",
    "\n== Color separability (camera) ==\n": "\n== 색 구분 능력 (카메라) ==\n",
    "Camera chroma / expected chroma (median): %1\n": "카메라 채도 / 기대 채도 (중앙값): %1\n",
    "WARNING: the camera sees very little color. Typical causes: halogen/IR-rich lighting without an\n"
    "IR-cut filter, or strong channel crosstalk. A CCM must amplify noise strongly here; fix the\n"
    "optics/lighting first if possible.\n":
    "경고: 카메라가 색을 거의 구분하지 못합니다. 흔한 원인: IR 차단 필터 없이 할로겐 등 적외선이\n"
    "많은 조명을 사용했거나, 채널 간 크로스토크가 큰 경우입니다. 이 상태에서는 CCM이 노이즈를\n"
    "크게 증폭하므로, 가능하면 광학계/조명을 먼저 개선하세요.\n",
    "No strongly different color pairs look alike to the camera.\n":
    "실제로 크게 다른 색이 카메라에서 비슷하게 보이는 경우는 없습니다.\n",
    "Pairs that differ in reality (ΔE00 ≥ 20) but are within 2x pixel noise in camera chromaticity.\n"
    "No 3x3 CCM can separate these; expect them to come out alike:\n":
    "실제로는 다르지만(ΔE00 ≥ 20) 카메라 색도 차이가 픽셀 노이즈의 2배 이내인 쌍입니다.\n"
    "어떤 3x3 CCM으로도 구분할 수 없으므로 비슷한 색으로 나옵니다:\n",
    "  %1 (%2)  vs  %3 (%4): distance %5 σ, reference ΔE00 %6\n":
    "  %1 (%2)  vs  %3 (%4): 거리 %5 σ, 기준 ΔE00 %6\n",
    "\n== Noise amplification ==\n": "\n== 노이즈 증폭 ==\n",
    "Row norms %1 / %2 / %3 (1.0 = no amplification), condition number %4\n":
    "행 노름 %1 / %2 / %3 (1.0 = 증폭 없음), 조건수 %4\n",
    "WARNING: large coefficients amplify sensor noise strongly (colored noise in flat areas).\n":
    "경고: 계수가 커서 센서 노이즈가 크게 증폭됩니다 (평탄한 영역에 컬러 노이즈).\n",
}

# Strings defined in the core library (translated in the "ccm" context).
CORE = {
    # illuminants
    "CIE A - Halogen / Incandescent (2856K)": "CIE A - 할로겐 / 백열등 (2856K)",
    "CIE F12 - U30 Fluorescent (3000K)": "CIE F12 - U30 형광등 (3000K)",
    "CIE LED-B2 - Warm White LED (3000K)": "CIE LED-B2 - 주백색 LED (3000K)",
    "CIE F11 - TL84 Fluorescent (4000K)": "CIE F11 - TL84 형광등 (4000K)",
    "CIE LED-B3 - Neutral White LED (4100K)": "CIE LED-B3 - 백색 LED (4100K)",
    "CIE F2 - Cool White Fluorescent (4230K)": "CIE F2 - 냉백색 형광등 (4230K)",
    "CIE D50 - Horizon Daylight (5003K)": "CIE D50 - 수평선 주광 (5003K)",
    "CIE LED-B4 - White LED (5100K)": "CIE LED-B4 - 백색 LED (5100K)",
    "CIE D55 - Mid-morning Daylight (5503K)": "CIE D55 - 오전 주광 (5503K)",
    "CIE LED-B5 - Cool White LED (6600K)": "CIE LED-B5 - 주광색 LED (6600K)",
    "CIE D65 - Noon Daylight (6504K)": "CIE D65 - 정오 주광 (6504K)",
    "CIE D75 - North Sky Daylight (7504K)": "CIE D75 - 북쪽 하늘 주광 (7504K)",
    "Tungsten halogen lamps, incandescent": "텅스텐 할로겐 램프, 백열등",
    "Warm white tri-band fluorescent": "온백색 3파장 형광등",
    "Phosphor-converted blue LED": "형광체 변환 청색 LED",
    "Store/office tri-band fluorescent": "매장/사무실 3파장 형광등",
    "CWF, standard fluorescent": "CWF, 표준 형광등",
    "Graphic arts viewing standard": "인쇄/그래픽 관찰 표준",
    "Photographic daylight": "사진용 주광",
    "sRGB / video reference white": "sRGB / 영상 기준 백색",
    "Overcast / shade": "흐린 날 / 그늘",
    "Blackbody locus": "흑체 궤적",
    "CIE daylight locus": "CIE 주광 궤적",
    # charts
    "X-Rite / Calibrite ColorChecker Classic 24": "X-Rite / Calibrite ColorChecker Classic 24",
    "ColorChecker Passport (Classic 24 page)": "ColorChecker Passport (Classic 24 페이지)",
    "ColorChecker Digital SG 140 (14x10)": "ColorChecker Digital SG 140 (14x10)",
    "Datacolor SpyderCHECKR 24 (6x4)": "Datacolor SpyderCHECKR 24 (6x4)",
    "Datacolor SpyderCHECKR 48 (8x6)": "Datacolor SpyderCHECKR 48 (8x6)",
    "IT8.7/2 (12x22 color area)": "IT8.7/2 (12x22 컬러 영역)",
    "Custom grid": "사용자 지정 격자",
    "Built-in reference: X-Rite post-2014 formulation (D50). Load the batch measurement file for best accuracy.":
    "내장 기준값: X-Rite 2014년 이후 배합 (D50). 정확도를 높이려면 해당 로트의 측정 파일을 불러오세요.",
    "Load the X-Rite reference file (CGATS) for your chart.": "사용 중인 차트의 X-Rite 기준값 파일(CGATS)을 불러오세요.",
    "Load the Datacolor reference file (CGATS/CSV).": "Datacolor 기준값 파일(CGATS/CSV)을 불러오세요.",
    "Load the batch CGATS file shipped with the target. Gray scale row is not part of this grid.":
    "타깃과 함께 제공된 로트별 CGATS 파일을 불러오세요. 그레이 스케일 행은 이 격자에 포함되지 않습니다.",
    "Set rows/columns and load a reference file.": "행/열을 설정하고 기준값 파일을 불러오세요.",
    # point mapping
    "Mark at least one gray/white point to neutralize the reference.":
    "기준 이미지를 중립화하려면 회색/흰색 점을 하나 이상 표시하세요.",
    # models
    "3x3": "3x3",
    "3x3 white-preserving": "3x3 백색 보존",
    "3x3 + offset (3x4)": "3x3 + 오프셋 (3x4)",
    "WB gains + 3x3 white-preserving": "WB 게인 + 3x3 백색 보존",
    "Root-polynomial 2nd order (3x6)": "루트 다항식 2차 (3x6)",
    # output spaces
    "sRGB / Rec.709 (D65)": "sRGB / Rec.709 (D65)",
    "Adobe RGB (1998) (D65)": "Adobe RGB (1998) (D65)",
}

# UI strings that are passed through tr() indirectly (not found by the extractor)
EXTRA_UI = set()


def extract_ui_strings():
    lit = re.compile(r'"((?:[^"\\]|\\.)*)"')
    call = re.compile(r'tr\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)')
    out = set()
    for f in glob.glob(os.path.join(ROOT, "src", "ui", "*.cpp")):
        s = open(f, encoding="utf-8").read()
        for m in call.finditer(s):
            raw = "".join(lit.findall(m.group(1)))
            out.add(raw.encode("utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8"))
    return out


def core_strings():
    """All string literals in src/core, with adjacent literals concatenated."""
    lit = re.compile(r'"((?:[^"\\]|\\.)*)"')
    seq = re.compile(r'((?:"(?:[^"\\]|\\.)*"\s*)+)')
    out = set()
    for f in glob.glob(os.path.join(ROOT, "src", "core", "*.cpp")):
        # drop char literals such as '"' first, they would unbalance the string matching
        text = re.sub(r"'(?:[^'\\]|\\.)'", "''", open(f, encoding="utf-8").read())
        for m in seq.finditer(text):
            out.add("".join(lit.findall(m.group(1))))
    return out


def c_escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


def main():
    ui = extract_ui_strings() | EXTRA_UI
    missing = sorted(ui - KO.keys())
    unused = sorted(KO.keys() - ui)
    core = core_strings()
    stale_core = sorted(k for k in CORE if c_escape(k) not in core)
    if missing:
        print("Missing Korean translations:\n  " + "\n  ".join(repr(m) for m in missing))
    if unused:
        print("Unused entries (text changed?):\n  " + "\n  ".join(repr(u) for u in unused))
    if stale_core:
        print("Core strings not found in src/core (text changed?):\n  " + "\n  ".join(repr(u) for u in stale_core))
    if missing or unused or stale_core:
        sys.exit(1)

    lines = ["// Generated by tools/gen_translations.py - do not edit by hand.",
             "// {English source text, Korean}"]
    for k in sorted(KO):
        lines.append('{"%s", "%s"},' % (c_escape(k), c_escape(KO[k])))
    for k in sorted(CORE):
        lines.append('{"%s", "%s"},' % (c_escape(k), c_escape(CORE[k])))
    path = os.path.join(ROOT, "src", "ui", "Translations_ko.inc")
    open(path, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
    print("wrote %s (%d UI + %d core strings)" % (path, len(KO), len(CORE)))


if __name__ == "__main__":
    main()


## 압축 파일 안내

- **presentation_resource.tar.gz**  
  발표 자료(중간발표자료,최종발표자료,포스터)가 모두 들어 있습니다.

- **operation_binary.tar.gz**  
  빌드된 바이너리 및 보드 이미지:
  - `Image` & `wt2837.dtb`  
    – 모든 보드에서 공통으로 사용되는 커널 이미지 및 디바이스 트리. TFTP로 설치하세요.
  - `server_ext2img.gz`  
    – 서버 보드용 루트 파일시스템. TFTP로 설치합니다.
  - `low_ext2img.gz`, `origin_ext2img.gz`, `high_ext2img.gz`  
    – 클라이언트 보드별(저역·원음·고역) 루트 파일시스템. 각 보드에 맞게 TFTP로 설치합니다.

- **source_code.tar.gz**  
  Linux 커널, RFS, DTB 설정을 제외한 모든 소스 코드와 Snapcast 빌드 바이너리가 들어 있습니다.  
  - `libs/` 폴더 내 공유 라이브러리를 반드시 대상 보드의 `/usr/lib` 아래에 복사해야 정상 동작합니다.  
  - RFS에 이미 라이브러리가 복사된 상태로 패키징되어 있습니다.
---
name: feedback-language-mixed
description: "대화는 한국어, 코드/문서 주석은 영어로 분리하는 규칙"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 0dfc9790-e0e2-4254-9f37-d246d45f192e
---

대화/응답은 한국어로 한다. 단, 코드와 코드 옆 문서(README, BUILDING, ADR, Stage 문서, CMake 주석 등)는 영어로 유지한다.

**Why:** 사용자가 처음에는 "한국어 → 영어"로 전환을 요청했으나(2026-05-27), 이후 다시 "한국어 대화 + 영어 코드/문서 주석"으로 명시 변경(2026-05-27 후속). 이유는 명시되지 않았으나, 본인 의사소통의 자연스러움(한국어)과 코드베이스 일관성/협업 가능성(영어 주석)을 분리해서 가져가려는 의도로 보인다.

**How to apply:**
- 응답 본문: 한국어
- 코드 주석 / 식별자: 영어 (식별자는 어차피 항상 영어)
- 코드 옆 마크다운 문서 (Docs/, README, BUILDING, ADR, Stage*.md): 영어
- 새 ADR 작성 시 영어
- 기존에 한국어로 작성된 Docs/Architecture.md, Stages/*, ADR/* 는 그대로 유지(append-only), 새로 작성하는 부분만 영어
- 대화 중 인용/참조하는 코드 스니펫은 영어로 유지
- 사용자가 한국어 코드 주석을 직접 작성/요청할 경우 그대로 따른다 (예외)

**Memory history:**
- 2026-05-27: 영어 전환 요청 ([[feedback-language-english]] - 폐기됨)
- 2026-05-27 (current): 한국어 대화 + 영어 코드/문서 주석 분리

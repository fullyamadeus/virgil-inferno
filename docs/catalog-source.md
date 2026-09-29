# Offline FINKI SEIS23 catalog

Source: [FINKI’s official English four-year SEIS23 program page](https://finki.ukim.mk/en/program/SEIS23/), inspected on **2026-09-27**.

`assets/finki-seis23.json` contains a factual snapshot of **157 unique course
codes/names**, the page's required/elective designation and listed/recommended
semester. It is compiled into the executable via Qt resources. It is not a full
regulatory study plan, prerequisite validator or admissions guide. Prerequisite
text and individual syllabi are not copied. Source changes require a reviewed
catalog update; normal runtime never downloads them. Manual course entry always
works, including missing or corrected codes and names.

Two source rows contradict their linked title's embedded code and are excluded:

| Table code | Different code in the linked title | Title |
| --- | --- | --- |
| F23L2S066 | F23L1S066 | Fundamentals of Cybersecurity |
| F23L1S026 | F23L2S026 | Marketing |

No code was silently guessed or substituted. The UI identifies this as the
English four-year SEIS23 snapshot, not the three-year variant, Macedonian SIIS23
or other FINKI curricula. The program page supplies the catalog's factual labels;
course placement is still the user's explicit choice when creating/editing a course.

To update: verify the official table again, review changed/contradictory codes,
update the JSON and its verification date, rebuild, and run the catalog tests.
Do not fetch anything at application startup or infer official data from old fixtures.

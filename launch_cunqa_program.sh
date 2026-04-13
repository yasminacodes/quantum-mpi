#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${SCRIPT_DIR}"
BUILD_DIR="${PROJECT_DIR}/build-qmio"
RUN_DIR="${PROJECT_DIR}/run"

DO_BUILD=0
if [[ "${1:-}" == "--build" ]]; then
    DO_BUILD=1
    shift
fi

TARGET_OR_PATH="${1:-quantum_mpi_main}"
if [[ $# -gt 0 ]]; then
    shift
fi
PROGRAM_ARGS=("$@")

export PATH="${HOME}/bin:${PATH}"
export SBATCH_EXPORT=ALL
export SLURM_EXPORT_ENV=ALL

resolve_slurm_stdout_path() {
    local job_id="$1"
    local stdout_path=""

    if [[ -z "${job_id}" ]]; then
        return 0
    fi

    if command -v scontrol >/dev/null 2>&1; then
        stdout_path="$(scontrol show job -o "${job_id}" 2>/dev/null \
            | sed -n 's/.* StdOut=\([^ ]*\).*/\1/p' \
            | head -n 1 || true)"
    fi

    if [[ -z "${stdout_path}" || "${stdout_path}" == "(null)" ]]; then
        if command -v sacct >/dev/null 2>&1; then
            stdout_path="$(sacct -j "${job_id}" --format=StdOut --parsable2 --noheader 2>/dev/null \
                | head -n 1 \
                | awk -F'|' '{print $1}' || true)"
        fi
    fi

    if [[ "${stdout_path}" == "(null)" ]]; then
        stdout_path=""
    fi

    if [[ -n "${stdout_path}" && "${stdout_path}" != /* ]]; then
        stdout_path="${RUN_DIR}/${stdout_path}"
    fi

    printf "%s" "${stdout_path}"
}

if command -v module >/dev/null 2>&1; then
    module --force purge >/dev/null 2>&1 || true
    module load qmio/hpc >/dev/null 2>&1 || true
    module load gcc/12.3.0 >/dev/null 2>&1 || true
    module load python/3.11.9 >/dev/null 2>&1 || true
    module load hpcx >/dev/null 2>&1 || module load hpcx-ompi >/dev/null 2>&1 || true
    module load imkl/2024.2 >/dev/null 2>&1 || true
    module load boost/1.85.0 >/dev/null 2>&1 || true
fi

if ! command -v qraise >/dev/null 2>&1; then
    echo "[ERROR] qraise not found in PATH."
    exit 1
fi

if [[ -x "${HOME}/bin/setup_qpus" ]]; then
    MISSING_SETUP_QPUS_LIBS="$(ldd "${HOME}/bin/setup_qpus" 2>/dev/null | awk '/not found/{print $1}')"
    if [[ -n "${MISSING_SETUP_QPUS_LIBS}" ]]; then
        echo "[ERROR] Missing runtime libraries for ${HOME}/bin/setup_qpus:"
        while IFS= read -r libname; do
            [[ -n "${libname}" ]] && echo "  - ${libname}"
        done <<< "${MISSING_SETUP_QPUS_LIBS}"
        echo "[ERROR] Aborting before launch."
        exit 1
    fi
fi

STORE_DEFAULT="/mnt/netapp1/Store_CESGA/${HOME#/}"
STORE_PATH="${STORE:-${STORE_DEFAULT}}"

if ! mkdir -p "${STORE_PATH}/.cunqa/logs" 2>/dev/null; then
    echo "[WARN] STORE path is not writable: ${STORE_PATH}"
    echo "[WARN] Falling back to HOME for this launch."
    STORE_PATH="${HOME}"
fi

if ! touch "${STORE_PATH}/.cunqa/logs/.write_test_$$" 2>/dev/null; then
    echo "[WARN] STORE logs path is not writable: ${STORE_PATH}/.cunqa/logs"
    echo "[WARN] Falling back to HOME for this launch."
    STORE_PATH="${HOME}"
    mkdir -p "${STORE_PATH}/.cunqa/logs"
else
    rm -f "${STORE_PATH}/.cunqa/logs/.write_test_$$"
fi

export STORE="${STORE_PATH}"
mkdir -p "${STORE}/.cunqa/logs"
if [[ ! -f "${STORE}/.cunqa/qpus.json" ]]; then
    printf "{}\n" > "${STORE}/.cunqa/qpus.json"
fi

echo "[INFO] Project: ${PROJECT_DIR}"
echo "[INFO] Build dir: ${BUILD_DIR}"
echo "[INFO] Run dir: ${RUN_DIR}"
echo "[INFO] STORE: ${STORE}"
echo "[INFO] qraise: $(command -v qraise || echo not-found)"

if [[ "${DO_BUILD}" -eq 1 ]]; then
    CUNQA_SOURCE_DIR="${QUANTUM_MPI_CUNQA_SOURCE_DIR:-${PROJECT_DIR}/cunqa}"
    ZMQ_INCLUDE_DIR="${QUANTUM_MPI_ZMQ_INCLUDE_DIR:-${HOME}/include}"
    ZMQ_LIBRARY="${QUANTUM_MPI_ZMQ_LIBRARY:-${HOME}/lib/libzmq.so}"
    SPDLOG_INCLUDE_DIR="${QUANTUM_MPI_SPDLOG_INCLUDE_DIR:-${CUNQA_SOURCE_DIR}/build/_deps/spdlog-src/include}"

    echo "[INFO] Build mode: ON (--build)"
    echo "[INFO] CUNQA source: ${CUNQA_SOURCE_DIR}"

    cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}" \
        -DQUANTUM_MPI_ENABLE_CUNQA_INTEGRATION=ON \
        -DQUANTUM_MPI_CUNQA_SOURCE_DIR="${CUNQA_SOURCE_DIR}" \
        -DQUANTUM_MPI_ZMQ_INCLUDE_DIR="${ZMQ_INCLUDE_DIR}" \
        -DQUANTUM_MPI_ZMQ_LIBRARY="${ZMQ_LIBRARY}" \
        -DQUANTUM_MPI_SPDLOG_INCLUDE_DIR="${SPDLOG_INCLUDE_DIR}"

    cmake --build "${BUILD_DIR}" --parallel "$(nproc)"
else
    echo "[INFO] Build mode: OFF (run only)"
fi

if [[ "${TARGET_OR_PATH}" == */* ]]; then
    EXEC_PATH="${TARGET_OR_PATH}"
else
    EXEC_PATH="${BUILD_DIR}/${TARGET_OR_PATH}"
fi

if [[ ! -x "${EXEC_PATH}" ]]; then
    echo "[ERROR] Executable not found: ${EXEC_PATH}"
    if [[ "${DO_BUILD}" -eq 0 ]]; then
        echo "[ERROR] Re-run with --build or compile manually first."
    fi
    exit 1
fi

mkdir -p "${RUN_DIR}"

TMP_OUTPUT="$(mktemp)"
set +e
(
    cd "${RUN_DIR}"
    "${EXEC_PATH}" "${PROGRAM_ARGS[@]}"
) 2>&1 | tee "${TMP_OUTPUT}"
PROGRAM_RC=${PIPESTATUS[0]}
set -e

JOB_ID="$(grep -Eo 'qraise job id:[[:space:]]*[0-9]+' "${TMP_OUTPUT}" | awk '{print $4}' | tail -n 1 || true)"
if [[ -z "${JOB_ID}" ]]; then
    JOB_ID="$(grep -Eo 'Submitted batch job [0-9]+' "${TMP_OUTPUT}" | awk '{print $4}' | tail -n 1 || true)"
fi
if [[ -z "${JOB_ID}" ]]; then
    JOB_ID="$(awk '/^[[:space:]]*[0-9]+[[:space:]]*$/{id=$1} END{print id}' "${TMP_OUTPUT}" || true)"
fi

RUN_QRAISE_FILE=""
QRAISE_TOKEN="$(grep -Eo 'qraise_[0-9]+' "${TMP_OUTPUT}" | tail -n 1 || true)"
STDOUT_PATH=""

if [[ -n "${JOB_ID}" ]]; then
    STDOUT_PATH="$(resolve_slurm_stdout_path "${JOB_ID}")"
fi

if [[ -z "${QRAISE_TOKEN}" && -n "${STDOUT_PATH}" ]]; then
    STDOUT_BASENAME="$(basename "${STDOUT_PATH}")"
    if [[ "${STDOUT_BASENAME}" =~ ^qraise_[0-9]+$ ]]; then
        QRAISE_TOKEN="${STDOUT_BASENAME}"
    fi
fi

rm -f "${TMP_OUTPUT}"

if [[ -n "${QRAISE_TOKEN}" ]]; then
    RUN_QRAISE_FILE="run/${QRAISE_TOKEN}"
elif [[ -n "${JOB_ID}" ]]; then
    RUN_QRAISE_FILE="run/qraise_${JOB_ID}"
fi

if [[ -n "${RUN_QRAISE_FILE}" ]]; then
    RUN_QRAISE_PATH="${PROJECT_DIR}/${RUN_QRAISE_FILE}"

    if [[ -n "${STDOUT_PATH}" && "${STDOUT_PATH}" != "${RUN_QRAISE_PATH}" ]]; then
        ln -sfn "${STDOUT_PATH}" "${RUN_QRAISE_PATH}" || true
    fi

    if [[ ! -e "${RUN_QRAISE_PATH}" && ! -L "${RUN_QRAISE_PATH}" ]]; then
        for _ in $(seq 1 10); do
            sleep 1
            if [[ -e "${RUN_QRAISE_PATH}" || -L "${RUN_QRAISE_PATH}" ]]; then
                break
            fi
        done
    fi

    if [[ -n "${JOB_ID}" ]]; then
        echo "[INFO] Execution id: ${JOB_ID}"
    fi
    echo "[INFO] Execution file: ${RUN_QRAISE_FILE}"
    if [[ -n "${STDOUT_PATH}" ]]; then
        echo "[INFO] Slurm StdOut path: ${STDOUT_PATH}"
    fi
    if [[ ! -e "${RUN_QRAISE_PATH}" && ! -L "${RUN_QRAISE_PATH}" ]]; then
        echo "[WARN] Execution file not present yet. The job may still be starting."
    fi
else
    if [[ -n "${JOB_ID}" ]]; then
        echo "[WARN] Execution id detected (${JOB_ID}) but execution file path could not be resolved."
    else
        echo "[WARN] Could not determine qraise execution id/file from output."
    fi
fi

exit "${PROGRAM_RC}"

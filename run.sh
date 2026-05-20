#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${SCRIPT_DIR}"
BUILD_DIR="${QUANTUM_MPI_BUILD_DIR:-${PROJECT_DIR}/build-qmio}"
RUN_ROOT_DIR="${QUANTUM_MPI_RUN_ROOT:-${PROJECT_DIR}/run}"
DETACH_MODE="${QUANTUM_MPI_DETACH:-1}"

if [[ $# -lt 1 ]]; then
    echo "[WARN] No executable selected. Nothing was launched."
    echo "[WARN] Usage: ./run.sh <executable-name|/full/path/to/executable> [args...]"
    if [[ -d "${BUILD_DIR}" ]]; then
        echo "[WARN] Available executables in ${BUILD_DIR}:"
        find "${BUILD_DIR}" \
            -maxdepth 2 \
            -type f \
            -executable \
            ! -name "*.so" \
            ! -name "*.a" \
            ! -name "*.o" \
            ! -path "*/CMakeFiles/*" \
            -print | sort
    fi
    exit 0
fi

TARGET_OR_PATH="${1}"
shift
PROGRAM_ARGS=("$@")

export PATH="${HOME}/bin:${PATH}"
export SBATCH_EXPORT=ALL
export SLURM_EXPORT_ENV=ALL
export QUANTUM_MPI_ENDPOINT_TIMEOUT_SECONDS="${QUANTUM_MPI_ENDPOINT_TIMEOUT_SECONDS:-300}"
export QUANTUM_MPI_PROJECT_DIR="${PROJECT_DIR}"
export QUANTUM_MPI_RESULTS_ROOT="${QUANTUM_MPI_RESULTS_ROOT:-${PROJECT_DIR}/results}"
export QUANTUM_MPI_CREATE_RESULTS_DIR="${QUANTUM_MPI_CREATE_RESULTS_DIR:-0}"

if command -v module >/dev/null 2>&1; then
    module --force purge >/dev/null 2>&1 || true
    module load qmio/hpc >/dev/null 2>&1 || true
    module load gcc/12.3.0 >/dev/null 2>&1 || true
    module load python/3.11.9 >/dev/null 2>&1 || true
    module load hpcx >/dev/null 2>&1 || module load hpcx-ompi >/dev/null 2>&1 || true
    module load imkl/2024.2 >/dev/null 2>&1 || true
    module load boost/1.85.0 >/dev/null 2>&1 || true
fi

add_ld_path() {
    local path="$1"
    if [[ -d "${path}" ]]; then
        if [[ -z "${LD_LIBRARY_PATH:-}" ]]; then
            export LD_LIBRARY_PATH="${path}"
        else
            export LD_LIBRARY_PATH="${path}:${LD_LIBRARY_PATH}"
        fi
    fi
}

add_ld_path "${PROJECT_DIR}/cunqa/build/lib"
add_ld_path "${PROJECT_DIR}/cunqa/build/_deps/maestro-build"
add_ld_path "${PROJECT_DIR}/cunqa/build/_deps/libzmq-build/lib"

if ! command -v qraise >/dev/null 2>&1; then
    echo "[ERROR] qraise not found in PATH."
    exit 1
fi

QRAISE_BIN="$(command -v qraise)"
QRAISE_LOG_ROOT=""
if command -v strings >/dev/null 2>&1; then
    QRAISE_LOG_ROOT="$(
        strings "${QRAISE_BIN}" 2>/dev/null | awk '/\.cunqa$/{print; exit}' || true
    )"
else
    echo "[WARN] 'strings' command not found. Skipping qraise static path check."
fi
if [[ -n "${QRAISE_LOG_ROOT}" ]]; then
    mkdir -p "${QRAISE_LOG_ROOT}/logs" 2>/dev/null || true
    if ! touch "${QRAISE_LOG_ROOT}/logs/.write_test_$$" 2>/dev/null; then
        echo "[WARN] qraise binary is configured with a non-writable CUNQA path:"
        echo "       ${QRAISE_LOG_ROOT}"
        echo "[WARN] qraise may fail before launch if that path remains read-only."
    else
        rm -f "${QRAISE_LOG_ROOT}/logs/.write_test_$$" 2>/dev/null || true
    fi
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
    printf "[]\n" > "${STORE}/.cunqa/qpus.json"
fi
export CUNQA_QPUS_FILE="${STORE}/.cunqa/qpus.json"

if [[ -z "${QUANTUM_MPI_ENDPOINT_COMMAND:-}" ]]; then
    export QUANTUM_MPI_ENDPOINT_COMMAND="awk -v job='{job_id}' 'match(\$0,/\"endpoint\"[[:space:]]*:[[:space:]]*\"([^\"]+)\"/,ep){endpoint=ep[1]} match(\$0,/\"slurm_job_id\"[[:space:]]*:[[:space:]]*\"([^\"]+)\"/,sj){if(sj[1]==job && endpoint!=\"\"){print endpoint; exit}}' \"${STORE}/.cunqa/qpus.json\" 2>/dev/null"
fi

if [[ "${TARGET_OR_PATH}" == */* ]]; then
    EXEC_PATH="$(readlink -f "${TARGET_OR_PATH}")"
else
    EXEC_PATH="${BUILD_DIR}/${TARGET_OR_PATH}"
fi

if [[ ! -x "${EXEC_PATH}" ]]; then
    echo "[ERROR] Executable not found: ${EXEC_PATH}"
    echo "[ERROR] Run ./configure first."
    exit 1
fi

mkdir -p "${RUN_ROOT_DIR}"
if [[ "${QUANTUM_MPI_CREATE_RESULTS_DIR}" == "1" ]]; then
    mkdir -p "${QUANTUM_MPI_RESULTS_ROOT}"
fi

LAUNCH_TS="$(date +%Y%m%d_%H%M%S)"
TARGET_LABEL="$(basename "${EXEC_PATH}")"
RUN_INSTANCE_DIR="${RUN_ROOT_DIR}/${LAUNCH_TS}_${TARGET_LABEL}"
mkdir -p "${RUN_INSTANCE_DIR}"
ln -sfn "${RUN_INSTANCE_DIR}" "${RUN_ROOT_DIR}/latest"

RUN_EXEC_PATH="${RUN_INSTANCE_DIR}/${TARGET_LABEL}"
cp -f "${EXEC_PATH}" "${RUN_EXEC_PATH}"
chmod +x "${RUN_EXEC_PATH}"

LAUNCH_LOG="${RUN_INSTANCE_DIR}/launcher.log"
PROGRAM_ARGS_PRETTY=""
if [[ ${#PROGRAM_ARGS[@]} -gt 0 ]]; then
    printf -v PROGRAM_ARGS_PRETTY '%q ' "${PROGRAM_ARGS[@]}"
    PROGRAM_ARGS_PRETTY="${PROGRAM_ARGS_PRETTY% }"
fi
if [[ -n "${PROGRAM_ARGS_PRETTY}" ]]; then
    PROGRAM_INVOCATION="${RUN_EXEC_PATH} ${PROGRAM_ARGS_PRETTY}"
else
    PROGRAM_INVOCATION="${RUN_EXEC_PATH}"
fi

{
    echo "[INFO] Project: ${PROJECT_DIR}"
    echo "[INFO] Build dir: ${BUILD_DIR}"
    echo "[INFO] Run root: ${RUN_ROOT_DIR}"
    echo "[INFO] Run instance: ${RUN_INSTANCE_DIR}"
    echo "[INFO] STORE: ${STORE}"
    echo "[INFO] qraise: $(command -v qraise || echo not-found)"
    echo "[INFO] Launch log: ${LAUNCH_LOG}"
    echo "[INFO] Results root: ${QUANTUM_MPI_RESULTS_ROOT}"
    echo "[INFO] Result delivery depends on the executable:"
    echo "[INFO]   - stdout/stderr are always in launcher.log"
    echo "[INFO]   - optional files (if implemented by the executable) may be written under ${QUANTUM_MPI_RESULTS_ROOT}"
    echo "[INFO] Optional qraise side files (if generated): ${RUN_INSTANCE_DIR}/qraise_<slurm_job_id>"
    echo "[INFO] Program invocation: ${PROGRAM_INVOCATION}"
} > "${LAUNCH_LOG}"

cd "${RUN_INSTANCE_DIR}"
if [[ "${DETACH_MODE}" == "0" ]]; then
    cat "${LAUNCH_LOG}"
    echo "[INFO] Launch mode: foreground"
    echo "[INFO] Program output will be streamed and saved to: ${LAUNCH_LOG}"
    echo "[INFO] Starting program..."
    echo "[INFO] Launch mode: foreground" >> "${LAUNCH_LOG}"
    echo "[INFO] Starting program..." >> "${LAUNCH_LOG}"

    set +e
    "${RUN_EXEC_PATH}" "${PROGRAM_ARGS[@]}" 2>&1 | tee -a "${LAUNCH_LOG}"
    EXEC_STATUS=${PIPESTATUS[0]}
    set -e

    cd "${PROJECT_DIR}"
    echo "[INFO] Program exited with code: ${EXEC_STATUS}"
    exit "${EXEC_STATUS}"
fi

echo "[INFO] Launch mode: background" >> "${LAUNCH_LOG}"
echo "[INFO] Starting program..." >> "${LAUNCH_LOG}"
nohup bash -c '
exec_path="$1"
log_path="$2"
shift 2

"${exec_path}" "$@" >> "${log_path}" 2>&1
status=$?
echo "[INFO] Program exited with code: ${status}" >> "${log_path}"
exit "${status}"
' _ "${RUN_EXEC_PATH}" "${LAUNCH_LOG}" "${PROGRAM_ARGS[@]}" &
LAUNCH_PID=$!
cd "${PROJECT_DIR}"

echo "[INFO] Project: ${PROJECT_DIR}"
echo "[INFO] Build dir: ${BUILD_DIR}"
echo "[INFO] Run root: ${RUN_ROOT_DIR}"
echo "[INFO] Run instance: ${RUN_INSTANCE_DIR}"
echo "[INFO] STORE: ${STORE}"
echo "[INFO] qraise: $(command -v qraise || echo not-found)"
echo "[INFO] Launch mode: background"
echo "[INFO] Launch PID: ${LAUNCH_PID}"
echo "[INFO] Launch log: ${LAUNCH_LOG}"
echo "[INFO] Follow log with: tail -f ${LAUNCH_LOG}"
echo "[INFO] Program output is being written to: ${LAUNCH_LOG}"
echo "[INFO] Results root: ${QUANTUM_MPI_RESULTS_ROOT}"
echo "[INFO] Result delivery depends on the executable:"
echo "[INFO]   - stdout/stderr are always in launcher.log"
echo "[INFO]   - optional files (if implemented by the executable) may be written under ${QUANTUM_MPI_RESULTS_ROOT}"
echo "[INFO] Optional qraise side files (if generated): ${RUN_INSTANCE_DIR}/qraise_<slurm_job_id>"
echo "[INFO] Launcher finished."

exit 0

LIBRARY()

LICENSE(BSD-3-Clause)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

VERSION(3.2.1)



NO_JOIN_SRC()

NO_COMPILER_WARNINGS()

NO_UTIL()

NO_RUNTIME()

IF (HAVE_MKL)
    PEERDIR(
        contrib/libs/intel/mkl
    )
ELSE()
    PEERDIR(
        contrib/libs/clapack/part1
        contrib/libs/clapack/part2
    )
ENDIF()

END()

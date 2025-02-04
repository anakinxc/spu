# Copyright 2021 Ant Group Co., Ltd.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


import time
import unittest

import multiprocess

import spu.libspu.link as link
import spu.psi as psi
from spu.tests.utils import get_free_port, wc_count
from spu.utils.simulation import PropagatingThread


class UnitTests(unittest.TestCase):
    def run_psi(self, fn):
        wsize = 2

        lctx_desc = link.Desc()
        for rank in range(wsize):
            lctx_desc.add_party(f"id_{rank}", f"thread_{rank}")

        def wrap(rank):
            lctx = link.create_mem(lctx_desc, rank)
            return fn(lctx)

        jobs = [PropagatingThread(target=wrap, args=(rank,)) for rank in range(wsize)]

        [job.start() for job in jobs]
        [job.join() for job in jobs]

    def run_streaming_psi(self, wsize, inputs, outputs, selected_fields, protocol):
        time_stamp = time.time()
        lctx_desc = link.Desc()
        lctx_desc.id = str(round(time_stamp * 1000))

        for rank in range(wsize):
            port = get_free_port()
            lctx_desc.add_party(f"id_{rank}", f"127.0.0.1:{port}")

        def wrap(rank, selected_fields, input_path, output_path, type):
            lctx = link.create_brpc(lctx_desc, rank)

            config = psi.BucketPsiConfig(
                psi_type=type,
                broadcast_result=True,
                input_params=psi.InputParams(
                    path=input_path, select_fields=selected_fields
                ),
                output_params=psi.OutputParams(path=output_path, need_sort=True),
                curve_type=psi.CurveType.CURVE_25519,
            )

            if type == psi.PsiType.DP_PSI_2PC:
                config.dppsi_params.bob_sub_sampling = 0.9
                config.dppsi_params.epsilon = 3

            report = psi.bucket_psi(lctx, config)

            source_count = wc_count(input_path)
            output_count = wc_count(output_path)
            print(
                f"id:{lctx.id()}, psi_type: {type}, original_count: {report.original_count}, intersection_count: {report.intersection_count}, source_count: {source_count}, output_count: {output_count}"
            )

            self.assertEqual(report.original_count, source_count - 1)
            self.assertEqual(report.intersection_count, output_count - 1)

            lctx.stop_link()

        # launch with multiprocess
        jobs = [
            multiprocess.Process(
                target=wrap,
                args=(
                    rank,
                    selected_fields,
                    inputs[rank],
                    outputs[rank],
                    protocol,
                ),
            )
            for rank in range(wsize)
        ]
        [job.start() for job in jobs]
        for job in jobs:
            job.join()
            self.assertEqual(job.exitcode, 0)

    def prep_data(self):
        data = [
            [f'r{idx}' for idx in range(1000) if idx % 3 == 0],
            [f'r{idx}' for idx in range(1000) if idx % 7 == 0],
        ]

        expected = [f'r{idx}' for idx in range(1000) if idx % 3 == 0 and idx % 7 == 0]

        return data, expected

    def test_reveal_to(self):
        data, expected = self.prep_data()
        expected.sort()

        reveal_to_rank = 0

        def fn(lctx):
            config = psi.MemoryPsiConfig(
                psi_type=psi.PsiType.KKRT_PSI_2PC,
                receiver_rank=reveal_to_rank,
                broadcast_result=False,
            )
            joint = psi.mem_psi(lctx, config, data[lctx.rank])

            joint.sort()

            if lctx.rank == reveal_to_rank:
                self.assertEqual(joint, expected)
            else:
                self.assertEqual(joint, [])

        self.run_psi(fn)

    def test_ecdh_3pc(self):
        print("----------test_ecdh_3pc-------------")

        inputs = [
            "spu/tests/data/alice.csv",
            "spu/tests/data/bob.csv",
            "spu/tests/data/carol.csv",
        ]
        outputs = ["./alice-ecdh3pc.csv", "./bob-ecdh3pc.csv", "./carol-ecdh3pc.csv"]
        selected_fields = ["id", "idx"]

        self.run_streaming_psi(
            3, inputs, outputs, selected_fields, psi.PsiType.ECDH_PSI_3PC
        )

    def test_dppsi_2pc(self):
        print("----------test_dppsi_2pc-------------")

        inputs = ["spu/tests/data/alice.csv", "spu/tests/data/bob.csv"]
        outputs = ["./alice-dppsi.csv", "./bob-dppsi.csv"]
        selected_fields = ["id", "idx"]

        self.run_streaming_psi(
            2, inputs, outputs, selected_fields, psi.PsiType.DP_PSI_2PC
        )


if __name__ == '__main__':
    unittest.main()

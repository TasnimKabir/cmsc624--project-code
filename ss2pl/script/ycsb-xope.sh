#ycsb-xope.sh(ss2pl)
tuple=100000000
maxope=10
rratio=95
rmw=off
skew=0.8
ycsb=on
cpu_mhz=2100
extime=3
epoch=3

host=`hostname`
chris41="chris41.omni.hpcc.jp"
dbs11="dbs11"

#basically
thread=24
if  test $host = $dbs11 ; then
thread=224
fi

cd ../
make clean; make -j KEY_SIZE=8 VAL_SIZE=4
cd script/

result=result_ss2pl-dlr1_ycsbB_tuple100m_skew08_ope10-100.dat
rm $result
echo "#Worker threads, avg-tps, min-tps, max-tps, avg-ar, min-ar, max-ar, avg-camiss, min-camiss, max-camiss" >> $result
echo "#sudo perf stat -e cache-misses,cache-references -o ana.txt numactl --interleave=all ../ss2pl.exe $tuple $maxope thread $rratio $rmw $skew $ycsb $cpu_mhz $extime" >> $result

for ((maxope=10; maxope<=100; maxope+=10))
do
  echo "sudo perf stat -e cache-misses,cache-references -o ana.txt numactl --interleave=all ../ss2pl.exe $tuple $maxope $thread $rratio $rmw $skew $ycsb $cpu_mhz $extime" 
  
  sumTH=0
  sumAR=0
  sumCA=0
  maxTH=0
  maxAR=0
  maxCA=0
  minTH=0
  minAR=0
  minCA=0
  for ((i=1; i <= epoch; i++))
  do
    if test $host = $dbs11 ; then
      sudo perf stat -e cache-misses,cache-references -o ana.txt numactl --interleave=all ../ss2pl.exe $tuple $maxope $thread $rratio $rmw $skew $ycsb $cpu_mhz $extime > exp.txt
    fi
    if test $host = $chris41 ; then
      perf stat -e cache-misses,cache-references -o ana.txt numactl --interleave=all ../ss2pl.exe $tuple $maxope $thread $rratio $rmw $skew $ycsb $cpu_mhz $extime > exp.txt
    fi
  
    tmpTH=`grep throughput ./exp.txt | awk '{print $2}'`
    tmpAR=`grep abort_rate ./exp.txt | awk '{print $2}'`
    tmpCA=`grep cache-misses ./ana.txt | awk '{print $4}'`
    sumTH=`echo "$sumTH + $tmpTH" | bc`
    sumAR=`echo "scale=4; $sumAR + $tmpAR" | bc | xargs printf %.4f`
    sumCA=`echo "$sumCA + $tmpCA" | bc`
    echo "tmpTH: $tmpTH, tmpAR: $tmpAR, tmpCA: $tmpCA"
  
    if test $i -eq 1 ; then
      maxTH=$tmpTH
      maxAR=$tmpAR
      maxCA=$tmpCA
      minTH=$tmpTH
      minAR=$tmpAR
      minCA=$tmpCA
    fi
  
    flag=`echo "$tmpTH > $maxTH" | bc`
    if test $flag -eq 1 ; then
      maxTH=$tmpTH
    fi
    flag=`echo "$tmpAR > $maxAR" | bc`
    if test $flag -eq 1 ; then
      maxAR=$tmpAR
    fi
    flag=`echo "$tmpCA > $maxCA" | bc`
    if test $flag -eq 1 ; then
      maxCA=$tmpCA
    fi
  
    flag=`echo "$tmpTH < $minTH" | bc`
    if test $flag -eq 1 ; then
      minTH=$tmpTH
    fi
    flag=`echo "$tmpAR < $minAR" | bc`
    if test $flag -eq 1 ; then
      minAR=$tmpAR
    fi
    flag=`echo "$tmpCA < $minCA" | bc`
    if test $flag -eq 1 ; then
      minCA=$tmpCA
    fi
  
  done
  avgTH=`echo "$sumTH / $epoch" | bc`
  avgAR=`echo "scale=4; $sumAR / $epoch" | bc | xargs printf %.4f`
  avgCA=`echo "$sumCA / $epoch" | bc`
  echo "sumTH: $sumTH, sumAR: $sumAR, sumCA: $sumCA"
  echo "avgTH: $avgTH, avgAR: $avgAR, avgCA: $avgCA"
  echo "maxTH: $maxTH, maxAR: $maxAR, maxCA: $maxCA"
  echo "minTH: $minTH, minAR: $minAR, minCA: $minCA"
  echo ""
  echo "$maxope $avgTH $minTH $maxTH $avgAR $minAR $maxAR, $avgCA $minCA $maxCA" >> $result
done

"""Maintain the owned evidence schemas (no runtime dependency or schema engine).

Cross-field arithmetic, canonical operation metadata, byte hashes, and attachment
integrity are additionally checked by EvidenceValidation.cpp.
"""
import json
from copy import deepcopy
from pathlib import Path
ROOT = Path(__file__).parent

def obj(**properties):
    return {'type':'object','additionalProperties':False,'required':list(properties),'properties':properties}
def const(v): return {'const':v}
def enum(*v): return {'enum':list(v)}
def arr(items, **kw): return {'type':'array','items':items,**kw}
def nullable(t): return {'anyOf':[t,{'type':'null'}]}
def ref(n): return {'$ref':'common.schema.json#/$defs/'+n}
S={'type':'string','minLength':1}; E={'type':'string'}; B={'type':'boolean'}; I={'type':'integer','minimum':0,'maximum':9223372036854775807}; P={**I,'minimum':1}; N={'type':'number','minimum':0}; H={'type':'string','pattern':'^[0-9a-f]{64}$'}; REV={'type':'string','pattern':'^[0-9a-f]{40}$'}; UTC={'type':'string','format':'date-time','pattern':'Z$'}; NULL={'type':'null'}
D={}
D['dimensions']=obj(width=P,height=P)
D['orientation']=obj(flipHorizontal=B,flipVertical=B,rotation=enum('degrees0','degrees90','degrees180','degrees270'))
D['source']=obj(canonicalName=S,canonicalEncoding=I,validBits={'type':'integer','minimum':1,'maximum':16},sampleMaximum={'type':'integer','minimum':1,'maximum':65535},packing=enum('unpacked'),alignment=enum('least_significant'),applicationStorage=enum('uint16','uint8'),nativeDimensions=ref('dimensions'),strideBytes=P,payloadBytes=P)
parameters=[{},dict(window={'type':'number'},level={'type':'number'}),dict(brightness={'type':'number'},contrast={'type':'number'}),dict(gamma={'type':'number'}),dict(clipLimit={'type':'number'},tileGridSize=P),dict(mode=enum('gaussian','median'),kernelSize=P,sigma={'type':'number'}),dict(amount={'type':'number'},radius={'type':'number'},threshold={'type':'number'}),{}]
ids=['normalize','window_level','brightness_contrast','gamma','clahe','denoise','sharpen','invert']
stages=[obj(id=const(i),enabled=B,parameters=obj(**p)) for i,p in zip(ids,parameters)]
D['pipeline']=obj(schemaVersion=const(1),orderVersion=const(1),configurationRevision=I,stages={'type':'array','minItems':8,'maxItems':8,'prefixItems':stages,'items':False})
D['provenance']=obj(source=obj(revision=nullable(REV),dirty=nullable(B),dirtyScope=const('git-status-porcelain-v1-untracked-normal'),statusSha256=nullable(H),capturedUtc=UTC),build=obj(configuration=nullable(S),compilerId=nullable(S),compilerVersion=nullable(S),cxxStandard=const(20),compileOptions=obj(scope=const('declared_cmake_configuration_and_target_options'),**{'global':E,'configuration':E,'processing':E,'evidence':E,'sourceOptions':E})),host=obj(osName=nullable(S),osVersion=nullable(S),architecture=nullable(S),cpuModel=nullable(S),logicalCores=nullable(P)),dependencies=obj(opencvVersion=nullable(S),opencvVcpkgPortVersion=nullable(S),vcpkgBaseline=nullable(REV),qtVersion=nullable(S)))
D['execution']=obj(backend=const('cpu'),threadModel=S,opencvThreads=I,roundingMode=const('FE_TONEAREST'),algorithmImplementation=S,algorithmProvenance=S)
stand=obj(scope=const('standalone_stage'),limitScope=const('standalone_scratch_only'),limitBytes=P,requiredBytes=P,fixedBytes=NULL,threeOwnerReserveBytes=NULL,gammaCacheReserveBytes=NULL,candidateRequiredBytes=NULL,boundedStatistics=obj(scratchBytes=I,stageOwnerBytes=I,separateHarnessImageBytes=I),exclusions=arr(S))
session=obj(scope=const('prepared_session'),limitScope=const('session_accounted_storage'),limitBytes=P,requiredBytes=P,fixedBytes=I,threeOwnerReserveBytes=I,gammaCacheReserveBytes=I,candidateRequiredBytes=I,boundedStatistics=obj(**dict.fromkeys(['externalSessionBytes','processingPoolBytes','displayPoolBytes','frameObjectBytes','orientationBytes','engineStateBytes','activationEnvelopeBytes','actualRetainedStageBytes'],I)),exclusions=arr(S))
D['resource']={'oneOf':[stand,session]}
D['sessionResource']=session
alloc=obj(scope=const('cxx_replacement_new'),calls=const(0),bytes=const(0),deallocations=const(0),armedRegion=S,coveredRoutes=const(['ordinary','array','aligned','aligned_array','nothrow','nothrow_array','aligned_nothrow','aligned_nothrow_array']),unsupportedRoutes=const(['c_malloc_free','external_dll_private_heaps']))
D['allocation']=alloc
D['measuredRegion']=obj(**alloc['properties'],cycles=P)
D['positiveControl']=obj(**{**alloc['properties'],'calls':const(8),'bytes':const(150),'deallocations':const(8)},cycles=const(1))
D['input']=obj(patternId=const('xorshift32_u16_v1'),version=const(1),seed=const(1831565813),sha256=H)
D['timing']=obj(unit=const('nanoseconds'),sampleCount=P,median={'type':'number','exclusiveMinimum':0,'multipleOf':0.5},p95NearestRank=P,wallElapsed=P,fps={'type':'number','exclusiveMinimum':0})
D['workingSet']={'oneOf':[obj(metric=const('unavailable'),source=const('unavailable'),beforeBytes=NULL,afterBytes=NULL),obj(metric=const('linux_resident_bytes'),source=const('proc_self_statm'),beforeBytes=P,afterBytes=P),obj(metric=const('windows_working_set_bytes'),source=const('GetProcessMemoryInfo'),beforeBytes=P,afterBytes=P)]}
D['checksum']=obj(algorithm=const('sha256'),value=H,provenance=S,verificationCycles=enum(0,1),measuredFingerprint=nullable({'type':'string','pattern':'^[0-9a-f]{1,16}$'}),fingerprintMethod=S)
D['failure']=nullable(obj(code=S,message=S,completedRows=I))
D['traceEvents']=obj(**dict.fromkeys(['events','successfulAllocationResults','nullAllocationResults','releases','reallocOldTransitions','reallocNewResults','reallocFailures','traceReportedSuccessfulBytes'],I))
provider=obj(path=S,sha256=H)
D['heapTrace']=nullable(obj(status=enum('complete','failed'),proof=B,cycles=P,tracePath=S,traceSha256=H,capability=obj(status=const('verified'),markerProvider=provider,endMarkerProvider=provider,mallocProvider=provider,glibcVersion=S,architecture=S,controlStatus=const('passed'),emptyControlStatus=const('passed'),controlTraceSha256=H,controlTracePath=S,controlEvents=ref('traceEvents'),emptyTraceSha256=H,emptyTracePath=S),events=ref('traceEvents'),scope=S,exclusions=arr(S)))
D['metrics']=obj(maxAbsoluteU16Error={'type':'integer','minimum':0,'maximum':65535},changedPixelFraction={'type':'number','minimum':0,'maximum':1},meanAbsoluteError={'type':'number','minimum':0,'maximum':65535})
D['comparison']={'oneOf':[ref('metrics'),obj(maxAbsoluteU16Error=NULL,changedPixelFraction=NULL,meanAbsoluteError=NULL)]}
D['payload']=obj(file={'type':'string','pattern':'^[^/\\\\]+\\.pgm$'},format=const('pgm_p5_u16'),width=P,height=P,maxValue=const(65535),byteOrder=const('big_endian'),payloadEncoding=enum('u16_big_endian','gray8_zero_extended_to_u16_big_endian'),sha256=H)
# Operation parameters retain strict known shapes; no generic arbitrary parameter map.
orientationParams=obj(terminalMapping=enum('nearest_positive_v_div_257','identity_u16'),orientation=ref('orientation'))
D['caseStage']=obj(id=enum(*ids,'orientation','full_standard_identity','full_standard_nonidentity'),variant=enum('standard','gaussian','median','gray8','gray16','identity','nonidentity'),enabled=const(True),scope=enum('standalone_stage','presentation_operations','full_frame'),fullStandardExecuted=B,sourceDomain=enum('sensor_native','canonical_u16','display'),outputStorage=enum('uint16','gray8'),parameters={'anyOf':[obj(**p) for p in parameters]+[orientationParams,ref('pipeline')]},implementation=S,provenance=S)
D['pattern']=obj(id=enum('ramp_u16_v1','gradient_xy_u16_v1','step_edges_u16_v1','xorshift32_u16_v1','orientation_asymmetric_u16_v1'),version=const(1),width=P,height=P,maximum={'type':'integer','minimum':1,'maximum':65535},seed=nullable(const(1831565813)))
case=dict(caseId=S,classification=enum('exact_independent','provisional_backend'),pattern=ref('pattern'),stage=ref('caseStage'),sourceDescriptor=ref('source'),orientation=ref('orientation'),orientedDimensions=ref('dimensions'),source=ref('payload'))
D['candidateCase']=obj(**case,candidate=ref('payload'),comparison=ref('comparison'),acceptance=obj(status=enum('candidate','pending_review'),thresholds=NULL,review=NULL))
D['committedCase']=obj(**case,expected=ref('payload'),acceptance={'oneOf':[obj(status=const('exact'),thresholds=obj(maxAbsoluteU16Error=const(0),changedPixelFraction=const(0),meanAbsoluteError=const(0)),review=NULL),obj(status=const('pending_review'),thresholds=NULL,review=NULL),obj(status=const('reviewed'),thresholds=ref('metrics'),review=obj(reviewedBy=S,reviewedUtc=UTC,sourceArtifactSha256=H))]})
base=dict(schemaVersion=const(1))
benchmarkRow=obj(rowId=enum('normalize','window_level','brightness_contrast','gamma','clahe','denoise_gaussian','denoise_median','sharpen','invert','full_standard_identity','full_standard_nonidentity'),scope=enum('standalone_stage','full_frame'),size={'type':'integer','minimum':8,'maximum':2147483647},smoke=B,pipelineDefinition=ref('pipeline'),input=ref('input'),sourceDescriptor=ref('source'),orientation=ref('orientation'),orientedDimensions=ref('dimensions'),provenance=ref('provenance'),execution=ref('execution'),resourcePlan=ref('resource'),warmUpFrames=P,measuredFrames=P,timing=ref('timing'),allocation=ref('allocation'),workingSet=ref('workingSet'),checksum=ref('checksum'),processingErrors=const(0),drops=const(0),complete=const(True))
benchmark=obj(**base,artifactType=const('lumora.processing.benchmark'),runStatus=enum('complete','incomplete'),complete=B,workload={'oneOf':[obj(kind=const('standard'),requestedSizes=const([512,1024,2048]),warmUpFrames=const(100),measuredFrames=const(500)),obj(kind=const('smoke'),requestedSizes=const([64,128]),warmUpFrames=const(2),measuredFrames=const(5)),obj(kind=const('custom'),requestedSizes=arr({'type':'integer','minimum':8,'maximum':2147483647},minItems=1,uniqueItems=True),warmUpFrames=P,measuredFrames=P)]},generatedUtc=UTC,rows=arr(benchmarkRow),failure=ref('failure'))
allocationRow=obj(rowId=enum('full_standard_identity','full_standard_nonidentity'),pipelineDefinition=ref('pipeline'),input=ref('input'),sourceDescriptor=ref('source'),orientation=ref('orientation'),orientedDimensions=ref('dimensions'),provenance=ref('provenance'),execution=ref('execution'),resourcePlan=ref('sessionResource'),warmUpFrames=enum(2,100),measuredCycles=enum(20,1000),positiveControl=ref('positiveControl'),measuredRegion=ref('measuredRegion'),heapTrace=ref('heapTrace'),processingErrors=const(0),drops=const(0),complete=const(True))
allocation=obj(**base,artifactType=const('lumora.processing.allocation-proof'),runStatus=enum('complete','incomplete'),complete=B,smoke=B,generatedUtc=UTC,rows=arr(allocationRow,maxItems=2),failure=ref('failure'))
reference={'oneOf':[obj(**base,artifactType=const('lumora.processing.reference-candidate'),status=const('candidate'),workloadKind=enum('candidate','smoke'),complete=B,generatedUtc=UTC,provenance=ref('provenance'),pipelineDefinition=ref('pipeline'),cases=arr(ref('candidateCase'),maxItems=13),failure=ref('failure')),obj(**base,artifactType=const('lumora.processing.reference-manifest'),status=enum('pending','reviewed'),sizeProfile=enum('ordinary','smoke'),complete=B,updatedUtc=UTC,pipelineDefinition=ref('pipeline'),cases=arr(ref('committedCase'),maxItems=13))]}
# A reviewed root binds every backend case to a reviewed acceptance object.
# Cross-artifact threshold/hash equality is checked by the typed validator;
# JSON Schema cannot compare values across the review bundle's files.
D['committedCase']['allOf']=[{
    'if':{'properties':{'classification':const('exact_independent')}},
    'then':{'properties':{'acceptance':{'properties':{'status':const('exact')}}}},
    'else':{'properties':{'acceptance':{'properties':{'status':enum('pending_review','reviewed')}}}},
}]
manifest=reference['oneOf'][1]
manifest['allOf']=[{
    'if':{'properties':{'status':const('reviewed')}},
    'then':{'properties':{
        'complete':const(True),
        'sizeProfile':const('ordinary'),
        'cases':{'minItems':13,'items':{
            'if':{'properties':{'classification':const('provisional_backend')}},
            'then':{'properties':{'acceptance':{'properties':{'status':const('reviewed')}}}},
        }},
    }},
    'else':{'properties':{'cases':{'items':{
        'if':{'properties':{'classification':const('provisional_backend')}},
        'then':{'properties':{'acceptance':{'properties':{'status':const('pending_review')}}}},
    }}}},
}]
facts={'machine':'manufacturer model firmware','cpu':'manufacturer model architecture physicalCores logicalCores','gpu':'manufacturer model dedicatedMemoryBytes','ram':'installedBytes speedMtPerSecond','os':'name edition version build','compiler':'id version','dependencies':'opencvVersion opencvVcpkgPortVersion vcpkgBaseline qtVersion','drivers':'gpu chipset','power':'plan acPower thermalCondition','threading':'threadModel opencvThreads logicalProcessorAffinity','build':'configuration compileOptions sourceRevision sourceDirty artifactSha256'}
intFacts={'physicalCores','logicalCores','dedicatedMemoryBytes','installedBytes','speedMtPerSecond','opencvThreads'}
factShapes={k:nullable(obj(**{n:nullable(I if n in intFacts else B if n in {'acPower','sourceDirty'} else S) for n in names.split()})) for k,names in facts.items()}
workstation=obj(**base,artifactType=const('lumora.processing.reference-workstation'),status=enum('pending','accepted'),designation=obj(machineId=nullable(S),selectedBy=nullable(S),selectedUtc=nullable(UTC)),**factShapes,requirements=const({'standard2048P95MaxMilliseconds':33.3,'sustainedFpsMinimum':30.0,'requiredEvidence':['full_standard_benchmark','allocation_proof','reviewed_reference_manifest','freshness_60fps']}),acceptance=obj(benchmarkArtifactSha256=nullable(H),allocationArtifactSha256=nullable(H),referenceManifestSha256=nullable(H),freshnessArtifactSha256=nullable(H),approvedThresholds=nullable(arr(obj(caseId=S,**D['metrics']['properties']),minItems=6,maxItems=6)),approvedReferenceHashes=nullable(arr(obj(caseId=S,sourceSha256=H,expectedSha256=H),minItems=6,maxItems=6)),reviewedBy=nullable(S),reviewedUtc=nullable(UTC)))
peripheral={'manufacturer','firmware','dedicatedMemoryBytes','speedMtPerSecond','chipset','thermalCondition','logicalProcessorAffinity'}
acceptedFacts={k:obj(**{n:(nullable(I if n in intFacts else S) if n in peripheral else I if n in intFacts else B if n in {'acPower','sourceDirty'} else S) for n in names.split()}) for k,names in facts.items()}
acceptedAcceptance={k:{'not':{'type':'null'}} for k in workstation['properties']['acceptance']['properties']}
workstation['allOf']=[{'if':{'properties':{'status':const('accepted')}},'then':{'properties':{**acceptedFacts,'designation':obj(machineId=S,selectedBy=S,selectedUtc=UTC),'acceptance':{'properties':acceptedAcceptance}}},'else':{'properties':{'acceptance':obj(**dict.fromkeys(acceptedAcceptance,NULL))}}}]
for artifact in [benchmark,allocation]:
    artifact['allOf']=[{'if':{'properties':{'complete':const(True)}},'then':{'properties':{'runStatus':const('complete'),'failure':NULL,'rows':{'minItems':2}}},'else':{'properties':{'runStatus':const('incomplete')}}}]
# Version 1 definitions above remain compatibility snapshots. Only the two
# executor-bearing artifacts acquire version 2; references/workstation stay v1.
sessionV2=deepcopy(session)
sessionV2['properties']['cpuExecutionSlots']={'type':'integer','minimum':1,'maximum':4}
sessionV2['properties']['cpuHelperThreads']={'type':'integer','minimum':0,'maximum':3}
sessionV2['required']+=['cpuExecutionSlots','cpuHelperThreads']
sessionV2['properties']['boundedStatistics']['properties']['cpuExecutorBytes']=P
sessionV2['properties']['boundedStatistics']['required'].append('cpuExecutorBytes')
sessionV2['properties']['exclusions']['contains']=const('thread_stacks_TLS_thread_library_and_OS_bookkeeping')
D['sessionResourceV2']=sessionV2
D['resourceV2']={'oneOf':[deepcopy(stand),sessionV2]}
D['helperTrace']=nullable(obj(status=const('passed'),tracePath=S,traceSha256=H,events=ref('traceEvents')))
D['helperControl']=obj(scope=const('prepared_cpu_executor_persistent_helpers'),expectedHelperMask=enum(0,2,6,14),observedHelperMask=enum(0,2,6,14),callbackInvocations={'type':'integer','minimum':0,'maximum':3},cxxAllocation=obj(scope=const('cxx_replacement_new'),calls={'type':'integer','minimum':0,'maximum':3},bytes=I,deallocations={'type':'integer','minimum':0,'maximum':3},armedRegion=const('caller reset/armed before one synchronous helper control dispatch and ended after return'),coveredRoutes=const(['ordinary']),unsupportedRoutes=const(['c_malloc_free','external_dll_private_heaps'])),glibcTrace=ref('helperTrace'))
benchmarkV2=deepcopy(benchmark)
benchmarkV2['properties']['schemaVersion']=const(2)
benchmarkV2['properties']['rows']['items']['properties']['resourcePlan']=ref('resourceV2')
allocationV2=deepcopy(allocation)
allocationV2['properties']['schemaVersion']=const(2)
allocationV2['properties']['rows']['items']['properties']['resourcePlan']=ref('sessionResourceV2')
allocationV2['properties']['rows']['items']['properties']['helperControl']=ref('helperControl')
allocationV2['properties']['rows']['items']['required'].append('helperControl')
inputV3=obj(patternId=const('xorshift32_u16_v1'),version=const(1),seed=const(1831565813),sha256=H,derivation=const('uint16(state >> 16) >> 4'))
sourceMono12=obj(canonicalName=const('Mono12'),canonicalEncoding=const(17825797),validBits=const(12),sampleMaximum=const(4095),packing=const('unpacked'),alignment=const('least_significant'),applicationStorage=const('uint16'),nativeDimensions=ref('dimensions'),strideBytes=P,payloadBytes=P)
benchmarkRowV3=deepcopy(benchmarkV2['properties']['rows']['items'])
benchmarkRowV3['properties']['rowId']=enum('full_standard_identity','full_standard_nonidentity')
benchmarkRowV3['properties']['scope']=const('full_frame')
benchmarkRowV3['properties']['input']=inputV3
benchmarkRowV3['properties']['sourceDescriptor']=sourceMono12
benchmarkRowV3['properties']['resourcePlan']=ref('sessionResourceV2')
benchmarkV3=deepcopy(benchmarkV2)
benchmarkV3['properties']['schemaVersion']=const(3)
benchmarkV3['properties']['sourceFormat']=const('mono12')
benchmarkV3['required'].append('sourceFormat')
benchmarkV3['properties']['rows']['items']=benchmarkRowV3
allocationRowV3=deepcopy(allocationV2['properties']['rows']['items'])
allocationRowV3['properties']['input']=inputV3
allocationRowV3['properties']['sourceDescriptor']=sourceMono12
allocationV3=deepcopy(allocationV2)
allocationV3['properties']['schemaVersion']=const(3)
allocationV3['properties']['sourceFormat']=const('mono12')
allocationV3['required'].append('sourceFormat')
allocationV3['properties']['rows']['items']=allocationRowV3
comment='Structural owned-artifact schema. EvidenceValidation.cpp additionally enforces canonical definitions/ordered row products, all cross-field arithmetic, duplicate decoded JSON keys, and on-disk payload and reviewed attachment integrity. Validation is not authority to designate or accept a workstation.'
common={'$schema':'https://json-schema.org/draft/2020-12/schema','$id':'common.schema.json','$defs':D}
(ROOT/'common.schema.json').write_text(json.dumps(common,indent=2)+'\n')
for name,schema in [('processing-benchmark',benchmark),('processing-allocation',allocation),('processing-benchmark-v2',benchmarkV2),('processing-allocation-v2',allocationV2),('processing-benchmark-v3',benchmarkV3),('processing-allocation-v3',allocationV3),('processing-reference',reference),('reference-workstation',workstation)]:
    document={'$schema':'https://json-schema.org/draft/2020-12/schema','$id':name+'.schema.json','$comment':comment,**schema}
    (ROOT/(name+'.schema.json')).write_text(json.dumps(document,indent=2)+'\n')
